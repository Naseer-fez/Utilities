use std::path::Path;
use std::sync::Arc;
use std::sync::mpsc;
use std::thread::sleep;
use std::time::Duration;
use crate::{log_info, log_debug, log_error};
use windows::Win32::Foundation::CloseHandle;
use windows::Win32::UI::WindowsAndMessaging::{GetForegroundWindow, GetWindowThreadProcessId};
use windows::Win32::System::Threading::{OpenProcess, PROCESS_QUERY_LIMITED_INFORMATION, QueryFullProcessImageNameW, PROCESS_NAME_WIN32};
use windows::core::PWSTR;

use super::idle_detector::IdleDetector;

pub struct ForegroundTracker {
    idle_detector: Arc<IdleDetector>,
    tx: mpsc::Sender<Option<String>>, // Sends the active foreground process name (or None)
    active_interval: Duration,
    idle_interval: Duration,
}

impl ForegroundTracker {
    pub fn new(
        idle_detector: Arc<IdleDetector>,
        tx: mpsc::Sender<Option<String>>,
        active_interval_ms: u64,
        idle_interval_ms: u64,
    ) -> Self {
        Self {
            idle_detector,
            tx,
            active_interval: Duration::from_millis(active_interval_ms),
            idle_interval: Duration::from_millis(idle_interval_ms),
        }
    }

    /// Helper to get the executable name of the active foreground window
    pub fn get_active_process_name(&self) -> Option<String> {
        unsafe {
            let hwnd = GetForegroundWindow();
            if hwnd.0.is_null() {
                return None;
            }

            let mut pid: u32 = 0;
            GetWindowThreadProcessId(hwnd, Some(&mut pid));
            
            if pid == 0 {
                return None;
            }

            Self::get_exe_name_from_pid(pid)
        }
    }

    pub fn get_exe_name_from_pid(pid: u32) -> Option<String> {
        if pid == 0 {
            return None;
        }
        unsafe {
            let handle = match OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, false, pid) {
                Ok(h) => h,
                Err(_) => {
                    return crate::modules::process_events::ProcessEventsObserver::get_exe_name_from_pid_fallback(pid);
                }
            };

            let mut buffer = [0u16; 1024];
            let mut size = buffer.len() as u32;
            let success = QueryFullProcessImageNameW(
                handle,
                PROCESS_NAME_WIN32,
                PWSTR(buffer.as_mut_ptr()),
                &mut size,
            ).is_ok();

            let _ = CloseHandle(handle);

            if success && size > 0 {
                let path_str = String::from_utf16_lossy(&buffer[..size as usize]);
                let path = Path::new(&path_str);
                if let Some(file_name) = path.file_name() {
                    return Some(file_name.to_string_lossy().to_string().to_lowercase());
                }
            }
        }
        None
    }

    /// Starts the polling loop (runs synchronously on a dedicated thread)
    pub fn run(self) {
        log_info!("Starting foreground window tracker loop");
        let mut last_reported: Option<String> = None;
        let mut first_run = true;

        loop {
            let is_idle = self.idle_detector.is_idle();
            
            let current_app = if is_idle {
                None // If idle, treat as no application having foreground focus
            } else {
                self.get_active_process_name()
            };

            // Report changes
            if first_run || current_app != last_reported {
                log_debug!("Foreground focus changed to: {:?}", current_app);
                if let Err(e) = self.tx.send(current_app.clone()) {
                    log_error!("Failed to send foreground focus update: {}", e);
                    break; // Receiver hung up, terminate loop
                }
                last_reported = current_app;
                first_run = false;
            }

            // Adaptive polling sleep
            if is_idle {
                sleep(self.idle_interval);
            } else {
                sleep(self.active_interval);
            }
        }
    }
}
