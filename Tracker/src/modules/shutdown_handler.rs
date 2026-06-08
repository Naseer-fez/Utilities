use std::sync::OnceLock;
use std::sync::mpsc;
use crate::{log_info, log_warn, log_error};

use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::UI::WindowsAndMessaging::{
    CreateWindowExW, DefWindowProcW, RegisterClassW, WNDCLASSW,
    WM_ENDSESSION, WM_QUERYENDSESSION, WM_POWERBROADCAST,
};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::core::w;

// Constants for WM_POWERBROADCAST
const PBT_APMSUSPEND: usize = 0x0004;
const PBT_APMRESUMEAUTOMATIC: usize = 0x0012;

#[derive(Debug, Clone)]
pub enum ShutdownMessage {
    Shutdown(mpsc::Sender<()>),
    Suspend,
    Resume,
}

static SHUTDOWN_TX: OnceLock<mpsc::Sender<ShutdownMessage>> = OnceLock::new();

/// Custom WndProc for the hidden window to intercept system shutdown and power events
unsafe extern "system" fn wnd_proc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    match msg {
        WM_QUERYENDSESSION => {
            log_info!("Received WM_QUERYENDSESSION: system is preparing to shutdown");
            if let Some(tx) = SHUTDOWN_TX.get() {
                let (done_tx, done_rx) = mpsc::channel();
                let _ = tx.send(ShutdownMessage::Shutdown(done_tx));
                // Wait up to 5 seconds for the main thread to complete flushing database
                let _ = done_rx.recv_timeout(std::time::Duration::from_secs(5));
            }
            LRESULT(1) // Return TRUE to indicate we are ready to end session
        }
        WM_ENDSESSION => {
            log_info!("Received WM_ENDSESSION: session is ending");
            if wparam.0 != 0 {
                if let Some(tx) = SHUTDOWN_TX.get() {
                    let (done_tx, done_rx) = mpsc::channel();
                    let _ = tx.send(ShutdownMessage::Shutdown(done_tx));
                    let _ = done_rx.recv_timeout(std::time::Duration::from_secs(5));
                }
            }
            LRESULT(0)
        }
        WM_POWERBROADCAST => {
            let event = wparam.0;
            if event == PBT_APMSUSPEND {
                log_warn!("Received PBT_APMSUSPEND: System is going to sleep!");
                if let Some(tx) = SHUTDOWN_TX.get() {
                    let _ = tx.send(ShutdownMessage::Suspend);
                }
            } else if event == PBT_APMRESUMEAUTOMATIC {
                log_info!("Received PBT_APMRESUMEAUTOMATIC: System has woken up!");
                if let Some(tx) = SHUTDOWN_TX.get() {
                    let _ = tx.send(ShutdownMessage::Resume);
                }
            }
            LRESULT(1)
        }
        _ => DefWindowProcW(hwnd, msg, wparam, lparam),
    }
}

pub struct ShutdownHandler {}

impl ShutdownHandler {
    /// Creates a hidden utility window that registers to receive system events in a background thread
    pub fn new(tx: mpsc::Sender<ShutdownMessage>) -> Self {
        // Store sender globally for wnd_proc access
        if SHUTDOWN_TX.set(tx).is_err() {
            log_warn!("Shutdown sender already initialized");
        }

        std::thread::spawn(|| {
            unsafe {
                let instance = match GetModuleHandleW(None) {
                    Ok(h) => h,
                    Err(e) => {
                        log_error!("GetModuleHandleW failed: {:?}", e);
                        return;
                    }
                };

                let class_name = w!("ExeTrackerHiddenWindowClass");
                let wnd_class = WNDCLASSW {
                    lpfnWndProc: Some(wnd_proc),
                    hInstance: instance.into(),
                    lpszClassName: class_name,
                    ..Default::default()
                };

                if RegisterClassW(&wnd_class) == 0 {
                    let err = windows::Win32::Foundation::GetLastError();
                    // Check if class already exists
                    if err != windows::Win32::Foundation::ERROR_CLASS_ALREADY_EXISTS {
                        log_error!("RegisterClassW failed: {:?}", err);
                        return;
                    }
                }

                let hwnd_res = CreateWindowExW(
                    Default::default(),
                    class_name,
                    w!("ExeTrackerHiddenWindow"),
                    Default::default(),
                    0, 0, 0, 0,
                    HWND::default(),
                    None,
                    instance,
                    None,
                );

                let _hwnd = match hwnd_res {
                    Ok(h) if !h.0.is_null() => {
                        log_info!("Hidden utility window created successfully for shutdown tracking");
                        h
                    }
                    Err(e) => {
                        log_error!("CreateWindowExW failed to create hidden window: {:?}", e);
                        return;
                    }
                    _ => {
                        log_error!("CreateWindowExW returned null handle");
                        return;
                    }
                };

                // Run message loop for this hidden window on this thread
                let mut msg = std::mem::zeroed();
                while windows::Win32::UI::WindowsAndMessaging::GetMessageW(&mut msg, HWND::default(), 0, 0).as_bool() {
                    let _ = windows::Win32::UI::WindowsAndMessaging::TranslateMessage(&msg);
                    windows::Win32::UI::WindowsAndMessaging::DispatchMessageW(&msg);
                }
            }
        });

        Self {}
    }
}
