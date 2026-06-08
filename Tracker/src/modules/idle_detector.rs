use windows::Win32::UI::Input::KeyboardAndMouse::{GetLastInputInfo, LASTINPUTINFO};
use windows::Win32::System::SystemInformation::GetTickCount;
use crate::{log_debug, log_error};

pub struct IdleDetector {
    threshold_ms: u32,
}

impl IdleDetector {
    pub fn new(threshold_secs: u64) -> Self {
        Self {
            threshold_ms: (threshold_secs * 1000) as u32,
        }
    }

    /// Returns the number of milliseconds since the last user input event
    pub fn get_idle_time_ms(&self) -> u32 {
        unsafe {
            let mut lii = LASTINPUTINFO {
                cbSize: std::mem::size_of::<LASTINPUTINFO>() as u32,
                dwTime: 0,
            };
            if GetLastInputInfo(&mut lii).as_bool() {
                let tick = GetTickCount();
                // Handle potential tick count overflow (wraps around every 49.7 days)
                tick.wrapping_sub(lii.dwTime)
            } else {
                log_error!("GetLastInputInfo failed");
                0
            }
        }
    }

    /// Checks if the user is currently idle (no input within the threshold)
    pub fn is_idle(&self) -> bool {
        let idle_time = self.get_idle_time_ms();
        let is_idle = idle_time >= self.threshold_ms;
        log_debug!("User idle time: {}ms, idle: {}", idle_time, is_idle);
        is_idle
    }
}
