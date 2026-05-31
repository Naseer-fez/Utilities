// src/notification/editor.rs

include!(concat!(env!("OUT_DIR"), "/editor.rs"));
use slint::{ComponentHandle, ModelRc, VecModel, Model};
use std::rc::Rc;

use crate::core::storage::{Reminder, RepeatConfig, load_reminders, save_reminders};
use chrono::{Local, NaiveTime, Duration};
use uuid::Uuid;

pub fn run_editor() -> Result<(), Box<dyn std::error::Error>> {
    let editor = EditorWindow::new()?;
    let editor_weak = editor.as_weak();

    // 1. Load reminders and populate the Slint Model
    let current_reminders = load_reminders().unwrap_or_else(|_| Vec::new());
    
    let model = Rc::new(VecModel::default());
    for r in &current_reminders {
        if r.enabled {
            model.push(ReminderItem {
                id: r.id.clone().into(),
                title: r.title.clone().into(),
                time: r.time.format("%Y-%m-%d %H:%M").to_string().into(),
                is_repeating: r.repeat.enabled,
            });
        }
    }
    
    editor.set_reminders(ModelRc::from(model.clone()));

    // Set default date to today and default time to next hour
    let now = Local::now();
    editor.set_default_date(now.format("%Y-%m-%d").to_string().into());
    editor.set_default_time((now + Duration::try_hours(1).unwrap()).format("%H:00").to_string().into());

    // 2. Delete callback
    editor.on_delete_task({
        let model_clone = model.clone();
        move |id_to_delete| {
            let id_str = id_to_delete.to_string();
            if let Ok(mut list) = load_reminders() {
                list.retain(|r| r.id != id_str);
                if let Err(e) = save_reminders(&list) {
                    log::error!("Failed to delete reminder: {}", e);
                    return;
                }
                
                // Update UI Model
                let mut found_idx = None;
                for i in 0..model_clone.row_count() {
                    if let Some(item) = model_clone.row_data(i) {
                        if item.id == id_to_delete {
                            found_idx = Some(i);
                            break;
                        }
                    }
                }
                if let Some(idx) = found_idx {
                    model_clone.remove(idx);
                }
            }
        }
    });

    // 3. Cancel callback
    editor.on_cancel({
        let editor_weak = editor_weak.clone();
        move || {
            if let Some(window) = editor_weak.upgrade() {
                window.hide().unwrap();
                std::process::exit(0);
            }
        }
    });

    // 4. Save task callback
    editor.on_save_task({
        let editor_weak = editor_weak.clone();
        move |title: slint::SharedString, message: slint::SharedString, date_str: slint::SharedString, time_str: slint::SharedString, repeat_enabled: bool, repeat_interval_str: slint::SharedString| {
            if title.trim().is_empty() {
                if let Some(window) = editor_weak.upgrade() {
                    window.set_error_message("Title cannot be empty".into());
                }
                return;
            }

            // Parse date
            let date_parsed = match chrono::NaiveDate::parse_from_str(date_str.as_str(), "%Y-%m-%d") {
                Ok(d) => d,
                Err(_) => {
                    if let Some(window) = editor_weak.upgrade() {
                        window.set_error_message("Invalid date format. Use YYYY-MM-DD".into());
                    }
                    return;
                }
            };

            // Parse time
            let time_parsed = match NaiveTime::parse_from_str(time_str.as_str(), "%H:%M") {
                Ok(t) => t,
                Err(_) => {
                    if let Some(window) = editor_weak.upgrade() {
                        window.set_error_message("Invalid time format. Use HH:MM".into());
                    }
                    return;
                }
            };

            // Parse repeat interval if enabled
            let mut interval_minutes = 60;
            if repeat_enabled {
                interval_minutes = match repeat_interval_str.as_str().parse::<i64>() {
                    Ok(v) if v > 0 => v,
                    _ => {
                        if let Some(window) = editor_weak.upgrade() {
                            window.set_error_message("Invalid repeat interval. Must be a positive integer".into());
                        }
                        return;
                    }
                };
            }

            // Construct target datetime and validate
            let target_datetime = date_parsed.and_time(time_parsed);
            let now = Local::now().naive_local();
            if target_datetime <= now {
                if let Some(window) = editor_weak.upgrade() {
                    window.set_error_message("Reminder time must be in the future".into());
                }
                return;
            }

            let new_reminder = Reminder {
                id: Uuid::new_v4().to_string(),
                title: title.to_string(),
                message: message.to_string(),
                time: target_datetime,
                repeat: RepeatConfig {
                    enabled: repeat_enabled,
                    interval_minutes,
                    remaining: -1,
                },
                image: None,
                enabled: true,
            };

            let mut reminders = load_reminders().unwrap_or_default();
            reminders.push(new_reminder);
            
            if let Err(e) = save_reminders(&reminders) {
                if let Some(window) = editor_weak.upgrade() {
                    window.set_error_message(format!("Failed to save: {}", e).into());
                }
                return;
            }

            if let Some(window) = editor_weak.upgrade() {
                window.hide().unwrap();
                std::process::exit(0);
            }
        }
    });

    editor.show()?;
    slint::run_event_loop()?;
    
    Ok(())
}
