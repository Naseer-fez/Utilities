use serde::{Deserialize, Serialize};
use std::fs;
use std::path::PathBuf;
use crate::{log_info, log_warn, log_error};

#[derive(Serialize, Deserialize, Debug, Clone, PartialEq)]
pub struct TrackedApp {
    pub exe_name: String,
    pub enabled: bool,
    pub category: Option<String>,
}

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct Config {
    pub tracking_enabled: bool,
    pub active_poll_interval_ms: u64,
    pub idle_poll_interval_ms: u64,
    pub idle_threshold_secs: u64,
    pub auto_start: bool,
    pub db_flush_interval_mins: u64,
    pub tracked_apps: Vec<TrackedApp>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            tracking_enabled: true,
            active_poll_interval_ms: 1000,   // 1 second active polling
            idle_poll_interval_ms: 5000,     // 5 seconds idle polling
            idle_threshold_secs: 300,        // 5 minutes
            auto_start: false,
            db_flush_interval_mins: 30,       // 30 minutes
            tracked_apps: vec![
                TrackedApp {
                    exe_name: "chrome.exe".to_string(),
                    enabled: true,
                    category: Some("Productivity".to_string()),
                },
                TrackedApp {
                    exe_name: "code.exe".to_string(),
                    enabled: true,
                    category: Some("Development".to_string()),
                },
            ],
        }
    }
}

pub struct ConfigManager {
    config_path: PathBuf,
    pub config: Config,
}

impl ConfigManager {
    pub fn new() -> Self {
        let app_data = std::env::var("APPDATA")
            .unwrap_or_else(|_| ".".to_string());
        let mut path = PathBuf::from(app_data);
        path.push("ExeTracker");
        
        // Ensure directory exists
        if let Err(e) = fs::create_dir_all(&path) {
            log_error!("Failed to create directory {}: {}", path.display(), e);
        }
        
        path.push("config.toml");
        
        let mut manager = Self {
            config_path: path,
            config: Config::default(),
        };
        
        manager.load();
        manager
    }

    pub fn load(&mut self) {
        if !self.config_path.exists() {
            log_info!("Config file does not exist, creating default at {}", self.config_path.display());
            self.save();
            return;
        }

        match fs::read_to_string(&self.config_path) {
            Ok(content) => {
                match toml::from_str::<Config>(&content) {
                    Ok(mut parsed_config) => {
                        let mut changed = false;
                        if parsed_config.active_poll_interval_ms == 120000 {
                            parsed_config.active_poll_interval_ms = 1000;
                            changed = true;
                        }
                        if parsed_config.idle_poll_interval_ms == 120000 {
                            parsed_config.idle_poll_interval_ms = 5000;
                            changed = true;
                        }
                        self.config = parsed_config;
                        log_info!("Successfully loaded config from {}", self.config_path.display());
                        if changed {
                            log_info!("Migrated legacy polling interval configuration values.");
                            self.save();
                        }
                    }
                    Err(e) => {
                        log_warn!("Failed to parse config: {}. Loading default config...", e);
                        self.config = Config::default();
                    }
                }
            }
            Err(e) => {
                log_error!("Failed to read config file {}: {}", self.config_path.display(), e);
                self.config = Config::default();
            }
        }
    }

    pub fn save(&self) {
        match toml::to_string_pretty(&self.config) {
            Ok(serialized) => {
                let mut temp_path = self.config_path.clone();
                temp_path.set_extension("tmp");
                if let Err(e) = fs::write(&temp_path, serialized) {
                    log_error!("Failed to write temp config file {}: {}", temp_path.display(), e);
                } else if let Err(e) = fs::rename(&temp_path, &self.config_path) {
                    log_error!("Failed to rename temp config to {}: {}", self.config_path.display(), e);
                    let _ = fs::remove_file(&temp_path);
                } else {
                    log_info!("Saved config to {}", self.config_path.display());
                }
            }
            Err(e) => {
                log_error!("Failed to serialize config: {}", e);
            }
        }
    }

    pub fn add_tracked_app(&mut self, exe_name: String, category: Option<String>) -> bool {
        let clean_exe = exe_name.trim().to_lowercase();
        if clean_exe.is_empty() {
            return false;
        }
        
        // Check if already exists
        if self.config.tracked_apps.iter().any(|app| app.exe_name.to_lowercase() == clean_exe) {
            return false;
        }
        
        self.config.tracked_apps.push(TrackedApp {
            exe_name: clean_exe,
            enabled: true,
            category,
        });
        self.save();
        true
    }

    pub fn remove_tracked_app(&mut self, exe_name: &str) -> bool {
        let clean_exe = exe_name.trim().to_lowercase();
        let initial_len = self.config.tracked_apps.len();
        self.config.tracked_apps.retain(|app| app.exe_name.to_lowercase() != clean_exe);
        
        if self.config.tracked_apps.len() != initial_len {
            self.save();
            true
        } else {
            false
        }
    }

    #[allow(dead_code)]
    pub fn is_tracked(&self, exe_name: &str) -> bool {
        let clean_exe = exe_name.trim().to_lowercase();
        self.config.tracked_apps.iter().any(|app| app.enabled && app.exe_name.to_lowercase() == clean_exe)
    }

    pub fn set_tracking_enabled(&mut self, enabled: bool) {
        self.config.tracking_enabled = enabled;
        self.save();
    }

    pub fn set_auto_start(&mut self, enabled: bool) {
        self.config.auto_start = enabled;
        self.save();
        
        // Update Windows Registry startup entry
        if let Err(e) = self.update_windows_registry(enabled) {
            log_error!("Failed to update registry for auto-start: {}", e);
        }
    }

    fn update_windows_registry(&self, enabled: bool) -> Result<(), Box<dyn std::error::Error>> {
        use windows::Win32::System::Registry::{
            RegCreateKeyExW, RegDeleteValueW, RegSetValueExW, HKEY_CURRENT_USER, REG_SZ,
            REG_OPTION_NON_VOLATILE, KEY_WRITE,
        };
        use windows::core::w;

        // Path to the CurrentVersion\Run registry key
        let sub_key = w!("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
        let value_name = w!("ExeTracker");

        unsafe {
            let mut hkey = HKEY_CURRENT_USER;
            
            let status = RegCreateKeyExW(
                HKEY_CURRENT_USER,
                sub_key,
                0,
                None,
                REG_OPTION_NON_VOLATILE,
                KEY_WRITE,
                None,
                &mut hkey,
                None,
            );

            if status.is_err() {
                return Err(format!("Failed to open/create registry key. Error code: {:?}", status).into());
            }

            if enabled {
                // Get current executable path
                let current_exe = std::env::current_exe()?;
                let mut path_str = current_exe.to_string_lossy().to_string();
                if path_str.starts_with(r"\\?\") {
                    path_str = path_str[4..].to_string();
                }
                
                // Add quotes in case path contains spaces
                let path_quoted = format!("\"{}\" --minimized", path_str);
                
                // Convert string to UTF-16
                let path_utf16: Vec<u16> = path_quoted.encode_utf16().chain(std::iter::once(0)).collect();
                
                let set_status = RegSetValueExW(
                    hkey,
                    value_name,
                    0,
                    REG_SZ,
                    Some(std::slice::from_raw_parts(
                        path_utf16.as_ptr() as *const u8,
                        path_utf16.len() * 2,
                    )),
                );
                
                let _ = windows::Win32::System::Registry::RegCloseKey(hkey);
                
                if set_status.is_err() {
                    return Err(format!("Failed to set registry value. Error code: {:?}", set_status).into());
                }
            } else {
                let del_status = RegDeleteValueW(hkey, value_name);
                let _ = windows::Win32::System::Registry::RegCloseKey(hkey);
                
                // Ignore key not found errors
                if del_status.is_err() && del_status != windows::Win32::Foundation::ERROR_FILE_NOT_FOUND {
                    return Err(format!("Failed to delete registry value. Error code: {:?}", del_status).into());
                }
            }
        }

        Ok(())
    }
}
