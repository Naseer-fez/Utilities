// src/notification/popup.rs - Entry point for the on-demand GUI popup subprocess

include!(concat!(env!("OUT_DIR"), "/popup.rs"));
use slint::ComponentHandle;

use std::time::{Duration, Instant};

#[cfg(target_os = "windows")]
fn get_bottom_right_position(width: u32, height: u32) -> (i32, i32) {
    extern "system" {
        fn GetSystemMetrics(nIndex: i32) -> i32;
    }
    unsafe {
        // SM_CXSCREEN = 0, SM_CYSCREEN = 1
        let screen_width = GetSystemMetrics(0);
        let screen_height = GetSystemMetrics(1);
        
        // Position 20px from right, 60px from bottom (approx taskbar height)
        let x = screen_width - (width as i32) - 20;
        let y = screen_height - (height as i32) - 60;
        (x, y)
    }
}

#[cfg(not(target_os = "windows"))]
fn get_bottom_right_position(width: u32, height: u32) -> (i32, i32) {
    (100, 100) // Fallback for other platforms
}

#[cfg(target_os = "windows")]
fn play_notification_sound() {
    extern "system" {
        fn MessageBeep(uType: u32) -> i32;
    }
    unsafe {
        // MB_ICONINFORMATION = 0x00000040 or MB_OK = 0
        MessageBeep(0x00000040);
    }
}

#[cfg(not(target_os = "windows"))]
fn play_notification_sound() {}

/// Runs the popup GUI in a dedicated blocking Slint loop.
/// Parses reminder data, sets up callbacks, drives the countdown, and exits.
pub fn run_popup(
    title: &str,
    message: &str,
    image_path: Option<&str>,
    duration_secs: u64,
    enable_sound: bool,
    width: u32,
    height: u32,
) -> Result<(), Box<dyn std::error::Error>> {
    // Create popup window
    let popup = ReminderPopup::new()?;

    // Set text contents
    popup.set_title_text(title.into());
    popup.set_message_text(message.into());

    // Resolve and load image only if requested
    let mut has_image = false;
    if let Some(ref path_str) = image_path {
        let img_path = crate::utils::resolve_path(path_str);
        if img_path.exists() {
            match slint::Image::load_from_path(&img_path) {
                Ok(img) => {
                    popup.set_icon_image(img);
                    has_image = true;
                }
                Err(e) => {
                    log::error!("Failed to load popup image: {}", e);
                }
            }
        } else {
            log::warn!("Specified image path does not exist: {:?}", img_path);
        }
    }
    popup.set_has_image(has_image);

    // Setup position at the bottom-right corner of screen
    let (x, y) = get_bottom_right_position(width, height);
    popup.window().set_position(slint::PhysicalPosition::new(x, y));

    // Handle Snooze Callback: Exit with status code 10
    popup.on_snooze_clicked(move || {
        log::info!("Snooze clicked, exiting with code 10");
        std::process::exit(10);
    });

    // Handle Close/Dismiss Callback: Exit with status code 0
    popup.on_close_clicked(move || {
        log::info!("Close/Done clicked, exiting with code 0");
        std::process::exit(0);
    });

    // Play Win32 notification beep if enabled
    if enable_sound {
        play_notification_sound();
    }

    // Set up draining progress bar timer running at ~30ms (approx 33 FPS)
    let start_time = Instant::now();
    let popup_weak = popup.as_weak();
    let total_duration = Duration::from_secs(duration_secs);

    let progress_timer = slint::Timer::default();
    progress_timer.start(
        slint::TimerMode::Repeated,
        Duration::from_millis(30),
        move || {
            if let Some(window) = popup_weak.upgrade() {
                let elapsed = start_time.elapsed();
                if elapsed >= total_duration {
                    log::info!("Popup timeout reached, auto-closing (exit 0)");
                    std::process::exit(0);
                } else {
                    let progress = 1.0 - (elapsed.as_secs_f32() / total_duration.as_secs_f32());
                    window.set_progress(progress);
                }
            }
        },
    );

    // Run Slint Event Loop (blocks until std::process::exit is called)
    popup.show()?;
    
    // Slint will block here until window closes normally, though our timers call std::process::exit
    slint::run_event_loop()?;
    
    Ok(())
}
