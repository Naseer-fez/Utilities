// src/core/storage.rs - Handles CRUD operations for reminders locally in reminders.json

use serde::{Deserialize, Serialize};
use chrono::NaiveDateTime;
use std::fs;
use crate::utils::resolve_path;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RepeatConfig {
    pub enabled: bool,
    pub interval_minutes: i64,
    pub remaining: i32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Reminder {
    pub id: String,
    pub title: String,
    pub message: String,
    pub time: NaiveDateTime,
    pub repeat: RepeatConfig,
    pub image: Option<String>,
    pub enabled: bool,
}

/// Loads all reminders from `reminders/reminders.json`.
pub fn load_reminders() -> Result<Vec<Reminder>, Box<dyn std::error::Error>> {
    let reminders_path = resolve_path("reminders/reminders.json");

    if !reminders_path.exists() {
        log::info!("Reminders file not found, starting with empty list");
        return Ok(Vec::new());
    }

    let content = fs::read_to_string(reminders_path)?;
    let reminders: Vec<Reminder> = serde_json::from_str(&content)?;
    Ok(reminders)
}

/// Saves all reminders to `reminders/reminders.json`.
pub fn save_reminders(reminders: &[Reminder]) -> Result<(), Box<dyn std::error::Error>> {
    let reminders_path = resolve_path("reminders/reminders.json");

    // Ensure parent directory exists
    if let Some(parent) = reminders_path.parent() {
        fs::create_dir_all(parent)?;
    }

    let serialized = serde_json::to_string_pretty(reminders)?;
    fs::write(reminders_path, serialized)?;
    Ok(())
}
