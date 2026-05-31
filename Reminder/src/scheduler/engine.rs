// src/scheduler/engine.rs - Core event-driven scheduler engine

use chrono::{Local, Duration as ChronoDuration};
use std::sync::mpsc;
use std::time::Duration;
use crate::core::config::AppSettings;
use crate::core::storage::{self, Reminder};
use crate::core::history::HistoryManager;

#[derive(Debug)]
pub enum SchedulerCommand {
    Reload,
    #[allow(dead_code)]
    Snooze(String, i64), // reminder_id, minutes
    Shutdown,
}

pub struct Scheduler {
    rx: mpsc::Receiver<SchedulerCommand>,
}

impl Scheduler {
    pub fn new(rx: mpsc::Receiver<SchedulerCommand>) -> Self {
        Self { rx }
    }

    /// Runs the main event-driven scheduler loop.
    pub fn run(self, settings: AppSettings) {
        log::info!("Starting background scheduler engine...");
        
        let mut history_manager = HistoryManager::new(settings.max_history_size);
        
        loop {
            // 1. Load active reminders
            let reminders = match storage::load_reminders() {
                Ok(list) => list,
                Err(e) => {
                    log::error!("Failed to load reminders: {}. Retrying in 10s...", e);
                    std::thread::sleep(Duration::from_secs(10));
                    continue;
                }
            };

            let now = Local::now().naive_local();

            // 2. Classify reminders: find missed ones and future ones
            let mut missed_reminders: Vec<Reminder> = reminders
                .iter()
                .filter(|r| r.enabled && r.time <= now)
                .cloned()
                .collect();

            // Sort missed reminders by trigger time (oldest first)
            missed_reminders.sort_by_key(|r| r.time);

            // 3. If there are missed reminders, trigger the first one immediately
            if let Some(reminder) = missed_reminders.first() {
                log::info!("Triggering missed reminder: {} (scheduled for: {})", reminder.title, reminder.time);
                
                // Trigger popup subprocess and wait for completion (blocks this thread)
                let exit_code = trigger_popup_subprocess(reminder, &settings);

                // Handle outcomes based on exit code (0 = Done, 10 = Snooze)
                if exit_code == 10 {
                    // Snooze clicked!
                    snooze_reminder(&reminder.id, settings.snooze_duration_minutes);
                } else {
                    // Normal Done / Dismiss / Auto-closed
                    // Log to history stack
                    history_manager.add(reminder.id.clone(), reminder.title.clone(), reminder.message.clone());
                    
                    // Complete reminder repeat logic
                    complete_reminder(reminder);
                }

                // Immediately loop to trigger the next pending reminder (sequential queue)
                continue;
            }

            // 4. No missed reminders! Find the next future reminder
            let mut future_reminders: Vec<Reminder> = reminders
                .iter()
                .filter(|r| r.enabled && r.time > now)
                .cloned()
                .collect();

            future_reminders.sort_by_key(|r| r.time);

            if let Some(next_reminder) = future_reminders.first() {
                let duration_until = next_reminder.time.signed_duration_since(now);
                let sleep_ms = duration_until.num_milliseconds().max(0) as u64;

                log::info!(
                    "Next reminder '{}' in {:.1} minutes (at {})",
                    next_reminder.title,
                    (sleep_ms as f64) / 60000.0,
                    next_reminder.time
                );

                // Wake up when either the timer fires or a command is received
                match self.rx.recv_timeout(Duration::from_millis(sleep_ms)) {
                    Ok(command) => {
                        if handle_command(command) {
                            break; // Shutdown
                        }
                    }
                    Err(mpsc::RecvTimeoutError::Timeout) => {
                        log::info!("Timer fired for: {}", next_reminder.title);
                        // Loop will continue and trigger it
                    }
                    Err(mpsc::RecvTimeoutError::Disconnected) => {
                        log::info!("Channel disconnected. Shutting down scheduler.");
                        break;
                    }
                }
            } else {
                log::info!("No active reminders scheduled. Sleeping until reloaded...");
                
                // Sleep indefinitely until command received
                match self.rx.recv() {
                    Ok(command) => {
                        if handle_command(command) {
                            break; // Shutdown
                        }
                    }
                    Err(_) => {
                        log::info!("Channel disconnected. Shutting down scheduler.");
                        break;
                    }
                }
            }
        }

        log::info!("Scheduler engine stopped.");
    }
}

/// Spawns the popup as an isolated, standalone child process of the same executable.
fn trigger_popup_subprocess(reminder: &Reminder, settings: &AppSettings) -> i32 {
    let current_exe = match std::env::current_exe() {
        Ok(path) => path,
        Err(e) => {
            log::error!("Failed to obtain current exe path: {}", e);
            return 0;
        }
    };

    // Construct args for Subprocess Mode
    let mut cmd = std::process::Command::new(current_exe);
    cmd.arg("--popup")
       .arg("--title").arg(&reminder.title)
       .arg("--message").arg(&reminder.message)
       .arg("--duration").arg(settings.popup_duration_seconds.to_string())
       .arg("--width").arg(settings.popup_width.to_string())
       .arg("--height").arg(settings.popup_height.to_string());

    if settings.enable_sound {
        cmd.arg("--sound");
    }

    if let Some(ref img_path) = reminder.image {
        cmd.arg("--image").arg(img_path);
    }

    log::info!("Launching popup subprocess for reminder id: {}", reminder.id);

    match cmd.status() {
        Ok(status) => {
            status.code().unwrap_or(0)
        }
        Err(e) => {
            log::error!("Popup subprocess execution failed: {}", e);
            0
        }
    }
}

/// Helper to handle incoming commands. Returns true if Shutdown command is received.
fn handle_command(command: SchedulerCommand) -> bool {
    match command {
        SchedulerCommand::Reload => {
            log::info!("Reload signal received. Refreshing scheduler.");
            false
        }
        SchedulerCommand::Snooze(id, minutes) => {
            log::info!("Snooze command received for reminder ID: {} ({} mins)", id, minutes);
            snooze_reminder(&id, minutes);
            false
        }
        SchedulerCommand::Shutdown => {
            log::info!("Shutdown command received. Stopping scheduler.");
            true
        }
    }
}

/// Snoozes a reminder by shifting its scheduled time forward.
fn snooze_reminder(reminder_id: &str, minutes: i64) {
    match storage::load_reminders() {
        Ok(mut list) => {
            let mut found = false;
            let mut title = String::new();
            let mut next_time = Local::now().naive_local();

            if let Some(reminder) = list.iter_mut().find(|r| r.id == reminder_id) {
                let now = Local::now().naive_local();
                reminder.time = now + ChronoDuration::minutes(minutes);
                reminder.enabled = true; // Make sure it's active
                
                found = true;
                title = reminder.title.clone();
                next_time = reminder.time;
            }

            if found {
                if let Err(e) = storage::save_reminders(&list) {
                    log::error!("Failed to save snoozed reminder: {}", e);
                } else {
                    log::info!("Reminder '{}' successfully snoozed for {} minutes (new time: {})", title, minutes, next_time);
                }
            } else {
                log::warn!("Snooze target reminder ID not found: {}", reminder_id);
            }
        }
        Err(e) => {
            log::error!("Failed to load reminders during snooze: {}", e);
        }
    }
}

/// Completes a reminder, decrementing repeat counts or disabling it.
fn complete_reminder(reminder: &Reminder) {
    match storage::load_reminders() {
        Ok(mut list) => {
            if let Some(r) = list.iter_mut().find(|x| x.id == reminder.id) {
                if r.repeat.enabled {
                    if r.repeat.remaining > 0 {
                        r.repeat.remaining -= 1;
                        if r.repeat.remaining == 0 {
                            r.repeat.enabled = false;
                            r.enabled = false;
                            log::info!("Reminder '{}' finished all repetitions and is now disabled.", r.title);
                        } else {
                            r.time = Local::now().naive_local() + ChronoDuration::minutes(r.repeat.interval_minutes);
                            log::info!("Reminder '{}' repeated. Next trigger in {}m (at {}), remaining: {}", r.title, r.repeat.interval_minutes, r.time, r.repeat.remaining);
                        }
                    } else if r.repeat.remaining < 0 {
                        // Infinite repeats (remaining = -1)
                        r.time = Local::now().naive_local() + ChronoDuration::minutes(r.repeat.interval_minutes);
                        log::info!("Reminder '{}' (infinite repeat) rescheduled for: {}", r.title, r.time);
                    }
                } else {
                    r.enabled = false;
                    log::info!("One-time reminder '{}' completed.", r.title);
                }
            }
            
            // REMOVE completely any tasks that are now disabled
            list.retain(|r| r.enabled);

            if let Err(e) = storage::save_reminders(&list) {
                log::error!("Failed to save completed reminder state: {}", e);
            }
        }
        Err(e) => {
            log::error!("Failed to load reminders during completion: {}", e);
        }
    }
}
