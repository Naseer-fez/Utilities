use rusqlite::{params, Connection, Result};
use std::path::PathBuf;
use std::sync::Mutex;
use crate::{log_info, log_error};
use chrono::{DateTime, Utc, NaiveDate};

pub struct SqliteLogger {
    db_path: PathBuf,
    conn: Mutex<Connection>,
}

#[derive(Debug, Clone)]
pub struct DbSession {
    pub exe_name: String,
    pub start_time: DateTime<Utc>,
    pub end_time: DateTime<Utc>,
    pub total_duration_seconds: u64,
    pub foreground_duration_seconds: u64,
    pub date: String,
}

impl SqliteLogger {
    pub fn new() -> Self {
        let app_data = std::env::var("APPDATA")
            .unwrap_or_else(|_| ".".to_string());
        let mut path = PathBuf::from(app_data);
        path.push("ExeTracker");
        
        if let Err(e) = std::fs::create_dir_all(&path) {
            log_error!("Failed to create directory {}: {}", path.display(), e);
        }
        
        path.push("tracker.db");

        let conn = Connection::open(&path).expect("Failed to open SQLite database");
        
        // Apply performance pragmas
        let _ = conn.pragma_update(None, "journal_mode", "WAL");
        let _ = conn.pragma_update(None, "synchronous", "NORMAL");
        let _ = conn.pragma_update(None, "temp_store", "MEMORY");
        let _ = conn.pragma_update(None, "cache_size", "-64"); // Limit cache to 64KB memory to squeeze RAM
        let _ = conn.pragma_update(None, "busy_timeout", "5000"); // 5 second timeout for locking
        
        let logger = Self {
            db_path: path,
            conn: Mutex::new(conn),
        };
        
        if let Err(e) = logger.init_db() {
            log_error!("Failed to initialize database: {}", e);
        }
        logger
    }

    fn init_db(&self) -> Result<()> {
        let conn = self.conn.lock().unwrap();
        
        // Create tracked_apps table
        conn.execute(
            "CREATE TABLE IF NOT EXISTS tracked_apps (
                id INTEGER PRIMARY KEY,
                exe_name TEXT UNIQUE NOT NULL,
                enabled INTEGER DEFAULT 1,
                category TEXT
            );",
            [],
        )?;

        // Create usage_sessions table
        conn.execute(
            "CREATE TABLE IF NOT EXISTS usage_sessions (
                id INTEGER PRIMARY KEY,
                exe_name TEXT NOT NULL,
                start_time INTEGER NOT NULL,
                end_time INTEGER NOT NULL,
                total_duration_seconds INTEGER NOT NULL,
                foreground_duration_seconds INTEGER NOT NULL,
                date TEXT NOT NULL
            );",
            [],
        )?;

        log_info!("SQLite database initialized successfully at {}", self.db_path.display());
        Ok(())
    }

    pub fn sync_tracked_apps(&self, apps: &[super::config_manager::TrackedApp]) -> Result<()> {
        let mut conn = self.conn.lock().unwrap();
        let _tx = conn.transaction()?;
        
        // Use a block or direct execution on tx
        {
            let tx = &_tx;
            // Clear old apps
            tx.execute("DELETE FROM tracked_apps", [])?;
            
            // Insert new ones
            for app in apps {
                tx.execute(
                    "INSERT INTO tracked_apps (exe_name, enabled, category) VALUES (?, ?, ?)",
                    params![
                        app.exe_name.to_lowercase(),
                        if app.enabled { 1 } else { 0 },
                        app.category
                    ],
                )?;
            }
        }
        
        _tx.commit()?;
        Ok(())
    }

    pub fn insert_sessions(&self, sessions: &[DbSession]) -> Result<()> {
        if sessions.is_empty() {
            return Ok(());
        }
        
        let mut conn = self.conn.lock().unwrap();
        let _tx = conn.transaction()?;
        
        {
            let tx = &_tx;
            for session in sessions {
                tx.execute(
                    "INSERT INTO usage_sessions (
                        exe_name, start_time, end_time, total_duration_seconds, foreground_duration_seconds, date
                    ) VALUES (?, ?, ?, ?, ?, ?)",
                    params![
                        session.exe_name.to_lowercase(),
                        session.start_time.timestamp(),
                        session.end_time.timestamp(),
                        session.total_duration_seconds,
                        session.foreground_duration_seconds,
                        session.date
                    ],
                )?;
            }
        }
        
        _tx.commit()?;
        log_info!("Flushed {} sessions to SQLite", sessions.len());
        Ok(())
    }

    pub fn get_daily_stats(&self, date: NaiveDate) -> Result<Vec<(String, u64, u64)>> {
        let conn = self.conn.lock().unwrap();
        let date_str = date.format("%Y-%m-%d").to_string();
        
        let mut stmt = conn.prepare(
            "SELECT exe_name, 
                    SUM(total_duration_seconds) as total, 
                    SUM(foreground_duration_seconds) as fg
             FROM usage_sessions 
             WHERE date = ? 
             GROUP BY exe_name 
             ORDER BY fg DESC"
        )?;
        
        let rows = stmt.query_map([date_str], |row| {
            Ok((
                row.get::<_, String>(0)?,
                row.get::<_, u64>(1)?,
                row.get::<_, u64>(2)?,
            ))
        })?;
        
        let mut results = Vec::new();
        for row in rows {
            results.push(row?);
        }
        
        Ok(results)
    }

    pub fn get_db_path(&self) -> PathBuf {
        self.db_path.clone()
    }
}
