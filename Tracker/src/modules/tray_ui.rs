use std::sync::mpsc;
use std::sync::Arc;
use std::sync::atomic::Ordering;
use crate::{log_info, log_error, log_debug};
use tray_icon::{
    menu::{Menu, MenuItem, Submenu, PredefinedMenuItem, MenuEvent, CheckMenuItem},
    TrayIconBuilder, Icon,
};
use windows::Win32::Foundation::HWND;
use windows::Win32::UI::WindowsAndMessaging::{
    GetMessageW, TranslateMessage, DispatchMessageW, WM_NULL,
};
use windows::core::PCWSTR;

pub enum TrayCommand {
    ToggleTracking,
    ToggleAutoStart,
    AddApp(String),
    RemoveApp(String),
    OpenDataFolder,
    ShowReport,
    Exit,
}

#[derive(Debug, Clone)]
pub struct TrayUpdate {
    pub tracking_enabled: bool,
    pub auto_start: bool,
    pub tracked_apps: Vec<String>,
    pub today_stats: Vec<(String, String)>,
}

pub struct TrayUi {
    command_tx: mpsc::Sender<TrayCommand>,
    update_tx: mpsc::Sender<TrayUpdate>,
    thread_id: Arc<std::sync::atomic::AtomicU32>,
}

impl TrayUi {
    pub fn new(command_tx: mpsc::Sender<TrayCommand>) -> (Self, mpsc::Receiver<TrayUpdate>) {
        let (update_tx, update_rx) = mpsc::channel();
        let thread_id = Arc::new(std::sync::atomic::AtomicU32::new(0));
        (
            Self { command_tx, update_tx, thread_id },
            update_rx,
        )
    }

    /// Generates a clean 16x16 RGBA icon in memory.
    /// Emerald green for tracking, Slate gray for paused.
    fn create_icon(is_active: bool) -> Icon {
        let width = 16;
        let height = 16;
        let mut rgba = vec![0u8; width * height * 4];
        
        for pixel in rgba.chunks_exact_mut(4) {
            if is_active {
                // Vibrant Emerald Green (Productive)
                pixel[0] = 46;  // R
                pixel[1] = 204; // G
                pixel[2] = 113; // B
                pixel[3] = 255; // A
            } else {
                // Sleek Slate Gray (Paused)
                pixel[0] = 149; // R
                pixel[1] = 165; // G
                pixel[2] = 166; // B
                pixel[3] = 255; // A
            }
        }
        
        Icon::from_rgba(rgba, width as u32, height as u32).expect("Failed to create tray icon")
    }

    /// Initializes the tray icon and runs the event dispatch task
    pub fn start(
        &self,
        tracking_enabled: bool,
        auto_start: bool,
        tracked_apps: Vec<String>,
        today_stats: Vec<(String, String)>,
        update_rx: mpsc::Receiver<TrayUpdate>,
    ) {
        let cmd_tx = self.command_tx.clone();
        let thread_id_clone = self.thread_id.clone();
        
        // Spawn a native background thread for the Win32 message pump
        std::thread::spawn(move || {
            log_info!("Starting native system tray UI thread");
            
            let tid = unsafe { windows::Win32::System::Threading::GetCurrentThreadId() };
            thread_id_clone.store(tid, Ordering::SeqCst);
            
            // Build the initial icon and menu entirely on this background thread
            let initial_icon = Self::create_icon(tracking_enabled);
            let menu = Self::build_menu(tracking_enabled, auto_start, tracked_apps, today_stats);

            // Construct tray icon locally on this thread
            let tray = TrayIconBuilder::new()
                .with_menu(Box::new(menu))
                .with_tooltip("Habit Tracker (Active)")
                .with_icon(initial_icon)
                .build()
                .expect("Failed to build tray icon");

            // Event and update loop using blocking GetMessageW (CPU 0.00%)
            loop {
                let mut msg = unsafe { std::mem::zeroed() };
                unsafe {
                    if !GetMessageW(&mut msg, HWND::default(), 0, 0).as_bool() {
                        break;
                    }
                    let _ = TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                
                // Check for menu click events from tray-icon/muda (non-blocking)
                while let Ok(event) = MenuEvent::receiver().try_recv() {
                    let id = event.id.0.as_str();
                    log_debug!("Tray menu click: {}", id);
                    
                    match id {
                        "toggle_tracking" => {
                            let _ = cmd_tx.send(TrayCommand::ToggleTracking);
                        }
                        "toggle_autostart" => {
                            let _ = cmd_tx.send(TrayCommand::ToggleAutoStart);
                        }
                        "open_folder" => {
                            let _ = cmd_tx.send(TrayCommand::OpenDataFolder);
                        }
                        "show_report" => {
                            let _ = cmd_tx.send(TrayCommand::ShowReport);
                        }
                        "exit" => {
                            let _ = cmd_tx.send(TrayCommand::Exit);
                        }
                        id if id.starts_with("remove_app:") => {
                            let app = id.strip_prefix("remove_app:").unwrap().to_string();
                            let _ = cmd_tx.send(TrayCommand::RemoveApp(app));
                        }
                        "add_app" => {
                            // Spawn file picker in an isolated thread to not block the message pump
                            let tx = cmd_tx.clone();
                            std::thread::spawn(move || {
                                if let Some(exe_path) = Self::pick_executable() {
                                    let _ = tx.send(TrayCommand::AddApp(exe_path));
                                }
                            });
                        }
                        _ => {}
                    }
                }

                // Check for thread updates from the core (non-blocking)
                while let Ok(update) = update_rx.try_recv() {
                    log_debug!("Background tray thread received update command");
                    let menu = Self::build_menu(
                        update.tracking_enabled,
                        update.auto_start,
                        update.tracked_apps,
                        update.today_stats,
                    );
                    let icon = Self::create_icon(update.tracking_enabled);
                    let tooltip = if update.tracking_enabled {
                        "Habit Tracker (Active)"
                    } else {
                        "Habit Tracker (Paused)"
                    };
                    
                    tray.set_menu(Some(Box::new(menu)));
                    let _ = tray.set_icon(Some(icon));
                    let _ = tray.set_tooltip(Some(tooltip));
                }
            }
        });
    }

    /// Triggers an update to the tray menu
    pub fn update(
        &self,
        tracking_enabled: bool,
        auto_start: bool,
        tracked_apps: Vec<String>,
        today_stats: Vec<(String, String)>,
    ) {
        let update = TrayUpdate {
            tracking_enabled,
            auto_start,
            tracked_apps,
            today_stats,
        };
        if let Err(e) = self.update_tx.send(update) {
            log_error!("Failed to send update to background tray thread: {}", e);
            return;
        }
        
        let tid = self.thread_id.load(Ordering::SeqCst);
        if tid != 0 {
            unsafe {
                let _ = windows::Win32::UI::WindowsAndMessaging::PostThreadMessageW(
                    tid,
                    WM_NULL,
                    windows::Win32::Foundation::WPARAM(0),
                    windows::Win32::Foundation::LPARAM(0),
                );
            }
        }
    }

    /// Assembles the tray context menu
    fn build_menu(
        tracking_enabled: bool,
        auto_start: bool,
        tracked_apps: Vec<String>,
        today_stats: Vec<(String, String)>,
    ) -> Menu {
        let menu = Menu::new();

        // Title Header
        let header = MenuItem::with_id("header", "Habit Tracker v1.0", false, None);
        let _ = menu.append(&header);
        let _ = menu.append(&PredefinedMenuItem::separator());

        // Toggle tracking
        let tracking_label = if tracking_enabled { "Pause Tracking" } else { "Resume Tracking" };
        let toggle_tracking = MenuItem::with_id("toggle_tracking", tracking_label, true, None);
        let _ = menu.append(&toggle_tracking);

        // Stats Submenu
        let stats_submenu = Submenu::new("Today's Statistics", true);
        if today_stats.is_empty() {
            let _ = stats_submenu.append(&MenuItem::with_id("no_stats", "No tracking data today yet", false, None));
        } else {
            for (app, stat_str) in today_stats {
                let stat_item = MenuItem::with_id(
                    format!("stat:{}", app),
                    format!("{}: {}", app, stat_str),
                    false,
                    None,
                );
                let _ = stats_submenu.append(&stat_item);
            }
        }
        let _ = menu.append(&stats_submenu);

        // Tracked apps management submenu
        let apps_submenu = Submenu::new("Tracked Applications", true);
        let add_app_item = MenuItem::with_id("add_app", "+ Add Application (.exe)...", true, None);
        let _ = apps_submenu.append(&add_app_item);
        let _ = apps_submenu.append(&PredefinedMenuItem::separator());

        if tracked_apps.is_empty() {
            let _ = apps_submenu.append(&MenuItem::with_id("no_apps", "No applications tracked", false, None));
        } else {
            for app in tracked_apps {
                let remove_item = MenuItem::with_id(
                    format!("remove_app:{}", app),
                    format!("Remove: {}", app),
                    true,
                    None,
                );
                let _ = apps_submenu.append(&remove_item);
            }
        }
        let _ = menu.append(&apps_submenu);

        let _ = menu.append(&PredefinedMenuItem::separator());

        // Show Summary Report
        let show_report = MenuItem::with_id("show_report", "Show Summary Report", true, None);
        let _ = menu.append(&show_report);

        // Open folder
        let open_folder = MenuItem::with_id("open_folder", "Open Database Folder", true, None);
        let _ = menu.append(&open_folder);

        // Auto-start checkbox
        let autostart_item = CheckMenuItem::with_id(
            "toggle_autostart",
            "Auto-Start with Windows",
            true,
            auto_start,
            None,
        );
        let _ = menu.append(&autostart_item);

        let _ = menu.append(&PredefinedMenuItem::separator());

        // Exit app
        let exit = MenuItem::with_id("exit", "Exit Tracker", true, None);
        let _ = menu.append(&exit);

        menu
    }

    /// Windows File Dialog to select an executable
    fn pick_executable() -> Option<String> {
        use windows::Win32::UI::Controls::Dialogs::{
            GetOpenFileNameW, OPENFILENAMEW, OFN_FILEMUSTEXIST, OFN_PATHMUSTEXIST,
        };
        use windows::core::w;

        log_info!("Opening file dialog to pick an executable");
        
        let mut file_buf = [0u16; 1024];
        
        // Define title and filters
        let title = w!("Select Executable to Track");
        let filter = w!("Executables (*.exe)\0*.exe\0All Files (*.*)\0*.*\0");

        unsafe {
            let coinit_res = windows::Win32::System::Com::CoInitializeEx(
                None,
                windows::Win32::System::Com::COINIT_APARTMENTTHREADED
            );
            let com_initialized = coinit_res.is_ok() || coinit_res == windows::Win32::Foundation::S_FALSE;
            
            // Get active window as the modal owner of the file dialog to prevent it from slipping behind
            let owner = windows::Win32::UI::WindowsAndMessaging::GetForegroundWindow();
            
            let mut ofn = OPENFILENAMEW {
                lStructSize: std::mem::size_of::<OPENFILENAMEW>() as u32,
                hwndOwner: owner,
                lpstrFilter: PCWSTR(filter.as_ptr()),
                lpstrFile: windows::core::PWSTR(file_buf.as_mut_ptr()),
                nMaxFile: file_buf.len() as u32,
                lpstrFileTitle: windows::core::PWSTR(std::ptr::null_mut()),
                nMaxFileTitle: 0,
                lpstrInitialDir: PCWSTR(std::ptr::null()),
                lpstrTitle: PCWSTR(title.as_ptr()),
                Flags: OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST,
                ..Default::default()
            };

            let mut result = None;
            if GetOpenFileNameW(&mut ofn).as_bool() {
                // Find zero-terminator
                let end = file_buf.iter().position(|&c| c == 0).unwrap_or(file_buf.len());
                let full_path = String::from_utf16_lossy(&file_buf[..end]);
                let path = std::path::Path::new(&full_path);
                if let Some(file_name) = path.file_name() {
                    let exe_name = file_name.to_string_lossy().to_string();
                    log_info!("Selected executable: {}", exe_name);
                    result = Some(exe_name);
                }
            }
            
            if com_initialized {
                windows::Win32::System::Com::CoUninitialize();
            }
            
            result
        }
    }
}
