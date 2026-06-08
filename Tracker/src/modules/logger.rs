use std::sync::OnceLock;
use std::sync::Mutex;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Write;
use chrono::Local;

static LOG_FILE: OnceLock<Mutex<File>> = OnceLock::new();

pub fn init() -> Result<(), Box<dyn std::error::Error>> {
    let app_data = std::env::var("APPDATA")
        .unwrap_or_else(|_| ".".to_string());
    let mut log_path = std::path::PathBuf::from(app_data);
    log_path.push("ExeTracker");
    let _ = std::fs::create_dir_all(&log_path);
    log_path.push("tracker.log");

    let file = OpenOptions::new()
        .create(true)
        .append(true)
        .open(&log_path)?;

    let _ = LOG_FILE.set(Mutex::new(file));
    Ok(())
}

pub fn log(level: &str, msg: &str) {
    if let Some(mutex) = LOG_FILE.get() {
        if let Ok(mut file) = mutex.lock() {
            let now = Local::now().format("%Y-%m-%d %H:%M:%S");
            let _ = writeln!(file, "[{}] {} - {}", now, level, msg);
            let _ = file.flush();
        }
    }
}

#[macro_export]
macro_rules! log_info {
    ($($arg:tt)*) => {
        $crate::modules::logger::log("INFO", &format!($($arg)*));
    };
}

#[macro_export]
macro_rules! log_warn {
    ($($arg:tt)*) => {
        $crate::modules::logger::log("WARN", &format!($($arg)*));
    };
}

#[macro_export]
macro_rules! log_error {
    ($($arg:tt)*) => {
        $crate::modules::logger::log("ERROR", &format!($($arg)*));
    };
}

#[macro_export]
macro_rules! log_debug {
    ($($arg:tt)*) => {
        $crate::modules::logger::log("DEBUG", &format!($($arg)*));
    };
}
