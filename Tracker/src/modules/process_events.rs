use std::collections::HashSet;
use std::sync::{Arc, Mutex};
use std::sync::mpsc;
use std::thread::sleep;
use std::time::Duration;
use crate::{log_info, log_error, log_debug};

use windows::Win32::System::Diagnostics::ToolHelp::{
    CreateToolhelp32Snapshot, Process32FirstW, Process32NextW, PROCESSENTRY32W, TH32CS_SNAPPROCESS,
};
use windows::Win32::Foundation::CloseHandle;

#[derive(Debug, Clone)]
pub enum ProcessEvent {
    Started(String),
    Stopped(String),
}

pub struct ProcessEventsObserver {
    tx: mpsc::Sender<ProcessEvent>,
    tracked_apps: Arc<Mutex<HashSet<String>>>,
}

fn get_processes_snapshot() -> Vec<(u32, String)> {
    let mut processes = Vec::new();
    unsafe {
        let snapshot = match CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) {
            Ok(h) => h,
            Err(e) => {
                log_error!("CreateToolhelp32Snapshot failed: {:?}", e);
                return processes;
            }
        };
        
        let mut entry = PROCESSENTRY32W {
            dwSize: std::mem::size_of::<PROCESSENTRY32W>() as u32,
            ..std::mem::zeroed()
        };
        
        if Process32FirstW(snapshot, &mut entry).is_ok() {
            loop {
                let end = entry.szExeFile.iter().position(|&c| c == 0).unwrap_or(entry.szExeFile.len());
                let exe_name = String::from_utf16_lossy(&entry.szExeFile[..end]).to_lowercase();
                if !exe_name.is_empty() {
                    processes.push((entry.th32ProcessID, exe_name));
                }
                
                if Process32NextW(snapshot, &mut entry).is_err() {
                    break;
                }
            }
        }
        let _ = CloseHandle(snapshot);
    }
    processes
}

impl ProcessEventsObserver {
    pub fn new(tx: mpsc::Sender<ProcessEvent>, tracked_apps: Arc<Mutex<HashSet<String>>>) -> Self {
        Self { tx, tracked_apps }
    }

    /// Runs process events monitoring using snapshot polling
    pub fn run(self) {
        log_info!("Starting process events observer (Snapshot Polling Fallback)");
        self.run_snapshot_polling();
    }

    /// Snapshot polling loop (runs every 2 seconds to ensure high accuracy with low resource usage)
    fn run_snapshot_polling(self) {
        log_info!("Process events snapshot polling loop active");
        
        // Populate initial running state
        let mut running_tracked = {
            let tracked_apps_guard = self.tracked_apps.lock().unwrap();
            Self::get_running_tracked_processes(&tracked_apps_guard)
        };
        
        // Notify tracker about already running processes on startup
        for exe in &running_tracked {
            log_debug!("Already running tracked process detected on startup: {}", exe);
            if self.tx.send(ProcessEvent::Started(exe.clone())).is_err() {
                return;
            }
        }

        loop {
            // Polling interval is set to 2 seconds to ensure high accuracy
            sleep(Duration::from_secs(2));
            
            let current_tracked = {
                let tracked_apps_guard = self.tracked_apps.lock().unwrap();
                Self::get_running_tracked_processes(&tracked_apps_guard)
            };

            // Find started processes: in current_tracked but not in running_tracked
            for exe in &current_tracked {
                if !running_tracked.contains(exe) {
                    log_info!("Snapshot detected tracked process start: {}", exe);
                    if self.tx.send(ProcessEvent::Started(exe.clone())).is_err() {
                        return;
                    }
                }
            }

            // Find stopped processes: in running_tracked but not in current_tracked
            for exe in &running_tracked {
                if !current_tracked.contains(exe) {
                    log_info!("Snapshot detected tracked process stop: {}", exe);
                    if self.tx.send(ProcessEvent::Stopped(exe.clone())).is_err() {
                        return;
                    }
                }
            }

            running_tracked = current_tracked;
        }
    }

    /// Helper to capture only the currently running processes that are in our tracked set
    pub fn get_running_tracked_processes(tracked_apps: &HashSet<String>) -> HashSet<String> {
        get_processes_snapshot().into_iter()
            .map(|(_, name)| name)
            .filter(|name| tracked_apps.contains(name))
            .collect()
    }

    /// Helper to capture all currently running process image names on the system
    pub fn get_all_running_processes() -> HashSet<String> {
        get_processes_snapshot().into_iter()
            .map(|(_, name)| name)
            .collect()
    }

    /// Fallback to get process name from PID using Toolhelp Snapshot (bypasses ERROR_ACCESS_DENIED for elevated processes)
    pub fn get_exe_name_from_pid_fallback(pid: u32) -> Option<String> {
        get_processes_snapshot().into_iter()
            .find(|(p, _)| *p == pid)
            .map(|(_, name)| name)
    }
}
