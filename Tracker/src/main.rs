#![windows_subsystem = "windows"]

mod modules;

use modules::tracker_core::TrackerCore;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Initialize our lightweight logger
    modules::logger::init()?;

    std::panic::set_hook(Box::new(|panic_info| {
        log_error!("PANIC occurred: {:?}", panic_info);
    }));

    log_info!("--------------------------------------------------");
    log_info!("Starting Ultra-Lightweight Habit Tracker Service");
    log_info!("--------------------------------------------------");

    // Initialize and run the tracker orchestrator
    let tracker = TrackerCore::new();
    tracker.start();

    Ok(())
}
