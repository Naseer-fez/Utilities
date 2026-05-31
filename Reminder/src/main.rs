// src/main.rs - Main entry point and CLI dispatcher

#![windows_subsystem = "windows"] // Prevents console window from showing up on Windows

mod core;
mod scheduler;
mod notification;
mod utils;

use std::path::Path;
use winit::event_loop::{ControlFlow, EventLoop};
use tray_icon::{
    menu::{Menu, MenuItem, PredefinedMenuItem},
    TrayIconBuilder,
};
use crate::scheduler::{Scheduler, SchedulerCommand};

/// Native helper to open files using the default OS application.
/// On Windows, it uses cmd.exe /c start which has zero dependencies.
fn open_file_in_editor(path: &Path) {
    if !path.exists() {
        // If file doesn't exist, try to touch/create it first so the editor opens a valid file
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        let _ = std::fs::write(path, "");
    }

    #[cfg(target_os = "windows")]
    {
        let _ = std::process::Command::new("cmd")
            .args(&["/c", "start", "", &path.to_string_lossy()])
            .spawn();
    }
    #[cfg(not(target_os = "windows"))]
    {
        let _ = std::process::Command::new("xdg-open")
            .arg(path)
            .spawn();
    }
}

fn main() {
    // 1. Collect and parse CLI arguments
    let args: Vec<String> = std::env::args().collect();

    // Check if running in On-Demand Editor Subprocess mode
    if args.contains(&"--editor".to_string()) {
        env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();
        log::info!("Starting editor subprocess");
        if let Err(e) = notification::run_editor() {
            log::error!("Editor execution failed: {}", e);
            std::process::exit(1);
        }
        std::process::exit(0);
    }

    // Check if running in On-Demand Popup Subprocess mode
    if args.contains(&"--popup".to_string()) {
        // Parse popup arguments
        let mut title = "Reminder".to_string();
        let mut message = "".to_string();
        let mut image = None;
        let mut duration = 5;
        let mut sound = false;
        let mut width = 420;
        let mut height = 140;

        for i in 0..args.len() {
            match args[i].as_str() {
                "--title" => if i + 1 < args.len() { title = args[i+1].clone(); },
                "--message" => if i + 1 < args.len() { message = args[i+1].clone(); },
                "--image" => if i + 1 < args.len() { image = Some(args[i+1].clone()); },
                "--duration" => if i + 1 < args.len() { duration = args[i+1].parse().unwrap_or(5); },
                "--sound" => sound = true,
                "--width" => if i + 1 < args.len() { width = args[i+1].parse().unwrap_or(420); },
                "--height" => if i + 1 < args.len() { height = args[i+1].parse().unwrap_or(140); },
                _ => {}
            }
        }

        // Initialize simple logger for the popup subprocess
        env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();

        log::info!("Starting popup subprocess: {}", title);
        if let Err(e) = notification::run_popup(&title, &message, image.as_deref(), duration, sound, width, height) {
            log::error!("Popup execution failed: {}", e);
            std::process::exit(1);
        }
        std::process::exit(0);
    }

    // 2. We are in Background Daemon Mode!
    
    // Single instance check using TCP port binding
    let daemon_port = "127.0.0.1:29471";
    let listener = match std::net::TcpListener::bind(daemon_port) {
        Ok(l) => l,
        Err(_) => {
            // Daemon is already running! So just run the editor directly in this process and exit.
            env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();
            log::info!("Daemon is already running. Opening GUI editor...");
            if let Err(e) = notification::run_editor() {
                log::error!("Editor execution failed: {}", e);
                std::process::exit(1);
            }
            // Connect to daemon to signal it to reload reminders
            let _ = std::net::TcpStream::connect(daemon_port);
            std::process::exit(0);
        }
    };

    // Load config settings
    let settings = core::config::load_settings();

    // Initialize logger with level specified in settings
    let log_level = settings.log_level.as_str();
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or(log_level)).init();

    log::info!("Starting Reminder App Background Daemon...");

    // Create std channels for scheduler control
    let (scheduler_tx, scheduler_rx) = std::sync::mpsc::channel::<SchedulerCommand>();

    // Spawn a listener thread to accept TCP pings and reload the scheduler
    let tx_clone = scheduler_tx.clone();
    std::thread::spawn(move || {
        for stream in listener.incoming() {
            if let Ok(_) = stream {
                log::info!("TCP ping received, sending reload command to scheduler...");
                let _ = tx_clone.send(SchedulerCommand::Reload);
            }
        }
    });

    // Spawn the editor subprocess immediately (unless --silent is specified)
    if !args.contains(&"--silent".to_string()) {
        if let Ok(exe_path) = std::env::current_exe() {
            let child_result = std::process::Command::new(exe_path)
                .arg("--editor")
                .spawn();
                
            if let Ok(mut child) = child_result {
                let tx = scheduler_tx.clone();
                std::thread::spawn(move || {
                    let _ = child.wait();
                    let _ = tx.send(SchedulerCommand::Reload);
                });
            }
        }
    }

    // Spawn a standard OS thread for the scheduler (no heavy async runtime!)
    let settings_clone = settings.clone();
    std::thread::spawn(move || {
        let scheduler = Scheduler::new(scheduler_rx);
        scheduler.run(settings_clone);
    });

    // 3. Build System Tray Menu
    let menu = Menu::new();
    let edit_reminders_item = MenuItem::new("Edit Reminders", true, None);
    let show_history_item = MenuItem::new("Show History", true, None);
    let reload_item = MenuItem::new("Reload Reminders", true, None);
    let exit_item = MenuItem::new("Exit", true, None);

    let separator = PredefinedMenuItem::separator();
    menu.append_items(&[
        &edit_reminders_item,
        &show_history_item,
        &separator,
        &reload_item,
        &exit_item,
    ]).expect("Failed to construct tray menu");

    // Load or generate custom tray icon
    let icon_path = utils::resolve_path("assets/icon.png");
    let tray_icon = if icon_path.exists() {
        if let Ok(img) = image::open(&icon_path) {
            let rgba = img.to_rgba8();
            let (w, h) = rgba.dimensions();
            tray_icon::Icon::from_rgba(rgba.into_raw(), w, h).ok()
        } else {
            None
        }
    } else {
        None
    };

    let tray_icon = tray_icon.unwrap_or_else(|| {
        log::info!("Custom tray icon missing. Generating sleek in-memory fallback icon.");
        let (rgba, w, h) = utils::generate_fallback_icon_rgba();
        tray_icon::Icon::from_rgba(rgba, w, h).expect("Failed to build fallback icon")
    });

    // Create the system tray icon
    let _tray = TrayIconBuilder::new()
        .with_menu(Box::new(menu))
        .with_tooltip("Reminder Engine")
        .with_icon(tray_icon)
        .build()
        .expect("Failed to build system tray icon");

    // 4. Run the Winit Event Loop on the Main Thread (blocks indefinitely, 0% CPU idle)
    let event_loop = EventLoop::new().expect("Failed to create winit event loop");
    event_loop.set_control_flow(ControlFlow::Wait);

    // Drain any leftover events from the global menu event channel
    while let Ok(_) = tray_icon::menu::MenuEvent::receiver().try_recv() {}

    let scheduler_tx_clone = scheduler_tx.clone();

    event_loop.run(move |event, elwt| {
        match event {
            winit::event::Event::AboutToWait => {
                // Poll tray/menu click events in AboutToWait right before event loop sleeps
                if let Ok(click_event) = tray_icon::menu::MenuEvent::receiver().try_recv() {
                    if click_event.id == edit_reminders_item.id() {
                        log::info!("Opening GUI editor");
                        let exe_path = std::env::current_exe().unwrap();
                        let child_result = std::process::Command::new(exe_path)
                            .arg("--editor")
                            .spawn();
                            
                        if let Ok(mut child) = child_result {
                            let tx = scheduler_tx_clone.clone();
                            std::thread::spawn(move || {
                                let _ = child.wait();
                                let _ = tx.send(SchedulerCommand::Reload);
                            });
                        }

                    } else if click_event.id == show_history_item.id() {
                        log::info!("Opening reminder history list");
                        open_file_in_editor(&utils::resolve_path("reminders/history.json"));
                    } else if click_event.id == reload_item.id() {
                        log::info!("Reloading reminders...");
                        let _ = scheduler_tx_clone.send(SchedulerCommand::Reload);
                    } else if click_event.id == exit_item.id() {
                        log::info!("Exiting background daemon");
                        let _ = scheduler_tx_clone.send(SchedulerCommand::Shutdown);
                        elwt.exit();
                    }
                }
            }
            _ => {}
        }
    }).expect("Winit event loop run failed");
}
