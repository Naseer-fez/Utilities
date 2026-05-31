// src/core/config.rs - Handles parsing, writing and loading app settings

use serde::{Deserialize, Serialize};
use std::fs;
use crate::utils::resolve_path;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AppSettings {
    pub popup_duration_seconds: u64,
    pub max_history_size: usize,
    pub fade_in_ms: u64,
    pub fade_out_ms: u64,
    pub popup_width: u32,
    pub popup_height: u32,
    pub popup_position: String,
    pub enable_sound: bool,
    pub log_level: String,
    pub snooze_duration_minutes: i64,
}

impl Default for AppSettings {
    fn default() -> Self {
        Self {
            popup_duration_seconds: 5,
            max_history_size: 100,
            fade_in_ms: 300,
            fade_out_ms: 300,
            popup_width: 420,
            popup_height: 140,
            popup_position: "bottom_right".to_string(),
            enable_sound: false,
            log_level: "info".to_string(),
            snooze_duration_minutes: 5,
        }
    }
}

/// Loads app settings from `reminders/settings.json`.
/// Generates default settings on disk if the file doesn't exist.
pub fn load_settings() -> AppSettings {
    let settings_path = resolve_path("reminders/settings.json");
    
    if !settings_path.exists() {
        log::info!("Settings file missing, creating default settings.json");
        let default_settings = AppSettings::default();
        if let Err(e) = save_settings(&default_settings) {
            log::error!("Failed to write default settings: {}", e);
        }
        return default_settings;
    }

    match fs::read_to_string(&settings_path) {
        Ok(content) => match serde_json::from_str::<AppSettings>(&content) {
            Ok(settings) => settings,
            Err(e) => {
                log::error!("Failed to parse settings.json: {}. Using default settings.", e);
                AppSettings::default()
            }
        },
        Err(e) => {
            log::error!("Failed to read settings.json: {}. Using default settings.", e);
            AppSettings::default()
        }
    }
}

/// Saves app settings to `reminders/settings.json`.
pub fn save_settings(settings: &AppSettings) -> Result<(), Box<dyn std::error::Error>> {
    let settings_path = resolve_path("reminders/settings.json");
    
    // Ensure parent directory exists
    if let Some(parent) = settings_path.parent() {
        fs::create_dir_all(parent)?;
    }

    let serialized = serde_json::to_string_pretty(settings)?;
    fs::write(settings_path, serialized)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_default_settings() {
        let settings = AppSettings::default();
        assert_eq!(settings.popup_duration_seconds, 5);
        assert_eq!(settings.max_history_size, 100);
        assert!(!settings.enable_sound);
        assert_eq!(settings.snooze_duration_minutes, 5);
    }

    #[test]
    fn test_serde_settings() {
        let settings = AppSettings::default();
        let serialized = serde_json::to_string(&settings).unwrap();
        let deserialized: AppSettings = serde_json::from_str(&serialized).unwrap();
        assert_eq!(deserialized.popup_duration_seconds, settings.popup_duration_seconds);
        assert_eq!(deserialized.snooze_duration_minutes, settings.snooze_duration_minutes);
    }
}
