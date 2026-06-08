use std::collections::HashMap;
use chrono::{DateTime, Utc, Local};
use crate::{log_info, log_debug};
use super::sqlite_logger::DbSession;

#[derive(Debug, Clone)]
pub struct ActiveSession {
    pub exe_name: String,
    pub start_time: DateTime<Utc>,
    pub total_duration_seconds: u64,
    pub foreground_duration_seconds: u64,
    pub last_update_time: DateTime<Utc>,
}

pub struct SessionCache {
    pub sessions: HashMap<String, ActiveSession>,
    focused_exe: Option<String>,
}

/// Splits a tracking session into daily segments (in the user's Local timezone)
/// if it crosses one or more midnight boundaries.
fn split_session_by_days(
    exe_name: String,
    start_time: DateTime<Utc>,
    end_time: DateTime<Utc>,
    total_duration_seconds: u64,
    foreground_duration_seconds: u64,
) -> Vec<DbSession> {
    let mut segments = Vec::new();
    let mut current_start = start_time;
    
    loop {
        let local_start = current_start.with_timezone(&Local);
        let local_end = end_time.with_timezone(&Local);
        
        if local_start.date_naive() == local_end.date_naive() {
            let duration_ratio = if end_time == start_time {
                1.0
            } else {
                (end_time - current_start).num_seconds() as f64 / (end_time - start_time).num_seconds() as f64
            };
            
            let segment_total = (total_duration_seconds as f64 * duration_ratio).round() as u64;
            let segment_fg = (foreground_duration_seconds as f64 * duration_ratio).round() as u64;
            
            segments.push(DbSession {
                exe_name: exe_name.clone(),
                start_time: current_start,
                end_time,
                total_duration_seconds: segment_total,
                foreground_duration_seconds: segment_fg,
                date: local_start.format("%Y-%m-%d").to_string(),
            });
            break;
        } else {
            let next_day_date = local_start.date_naive() + chrono::Duration::days(1);
            let next_midnight_local = next_day_date.and_hms_opt(0, 0, 0).unwrap()
                .and_local_timezone(Local)
                .unwrap();
            let next_midnight_utc = next_midnight_local.with_timezone(&Utc);
            
            let duration_ratio = if end_time == start_time {
                0.0
            } else {
                (next_midnight_utc - current_start).num_seconds() as f64 / (end_time - start_time).num_seconds() as f64
            };
            
            let segment_total = (total_duration_seconds as f64 * duration_ratio).round() as u64;
            let segment_fg = (foreground_duration_seconds as f64 * duration_ratio).round() as u64;
            
            segments.push(DbSession {
                exe_name: exe_name.clone(),
                start_time: current_start,
                end_time: next_midnight_utc,
                total_duration_seconds: segment_total,
                foreground_duration_seconds: segment_fg,
                date: local_start.format("%Y-%m-%d").to_string(),
            });
            
            current_start = next_midnight_utc;
        }
    }
    
    // Adjust durations to match totals exactly to prevent rounding drifts
    let sum_total: u64 = segments.iter().map(|s| s.total_duration_seconds).sum();
    let sum_fg: u64 = segments.iter().map(|s| s.foreground_duration_seconds).sum();
    
    if !segments.is_empty() {
        if sum_total != total_duration_seconds {
            let diff = total_duration_seconds as i64 - sum_total as i64;
            let last_idx = segments.len() - 1;
            segments[last_idx].total_duration_seconds = (segments[last_idx].total_duration_seconds as i64 + diff).max(0) as u64;
        }
        if sum_fg != foreground_duration_seconds {
            let diff = foreground_duration_seconds as i64 - sum_fg as i64;
            let last_idx = segments.len() - 1;
            segments[last_idx].foreground_duration_seconds = (segments[last_idx].foreground_duration_seconds as i64 + diff).max(0) as u64;
        }
    }
    
    segments
}

impl SessionCache {
    pub fn new() -> Self {
        Self {
            sessions: HashMap::new(),
            focused_exe: None,
        }
    }

    /// Sets the currently focused executable
    pub fn set_focused_exe(&mut self, focused: Option<&str>) {
        self.focused_exe = focused.map(|s| s.to_lowercase());
    }

    /// Handles a process started event
    pub fn process_started(&mut self, exe_name: &str) {
        let exe_name_lower = exe_name.to_lowercase();
        let now = Utc::now();
        
        self.sessions.entry(exe_name_lower.clone()).or_insert_with(|| {
            log_info!("Starting tracking session for {}", exe_name_lower);
            ActiveSession {
                exe_name: exe_name_lower.clone(),
                start_time: now,
                total_duration_seconds: 0,
                foreground_duration_seconds: 0,
                last_update_time: now,
            }
        });
    }

    /// Internal process stopped handler returning segments
    pub fn process_stopped_internal(&mut self, exe_name: &str) -> Option<Vec<DbSession>> {
        let exe_name_lower = exe_name.to_lowercase();
        let now = Utc::now();
        
        if let Some(session) = self.sessions.remove(&exe_name_lower) {
            let elapsed = now.signed_duration_since(session.last_update_time).num_seconds().max(0) as u64;
            let final_total = session.total_duration_seconds + elapsed;
            let mut final_fg = session.foreground_duration_seconds;
            if let Some(ref fg_exe) = self.focused_exe {
                if fg_exe == &exe_name_lower {
                    final_fg += elapsed;
                }
            }
            
            log_info!("Ending tracking session for {}. Total: {}s, FG: {}s", 
                exe_name_lower, final_total, final_fg);
            
            return Some(split_session_by_days(
                session.exe_name.clone(),
                session.start_time,
                now,
                final_total,
                final_fg,
            ));
        }
        
        None
    }

    /// Handles a process stopped event. Returns finalized session segments.
    pub fn process_stopped(&mut self, exe_name: &str) -> Vec<DbSession> {
        self.process_stopped_internal(exe_name).unwrap_or_default()
    }

    /// Ticks the session cache to accumulate durations since the last update
    pub fn tick(&mut self, focused_exe: Option<&str>) {
        self.focused_exe = focused_exe.map(|s| s.to_lowercase());
        let now = Utc::now();
        
        for (exe, session) in self.sessions.iter_mut() {
            let elapsed = now.signed_duration_since(session.last_update_time).num_seconds().max(0) as u64;
            if elapsed == 0 {
                continue;
            }
            
            session.total_duration_seconds += elapsed;
            
            if let Some(ref fg_exe) = self.focused_exe {
                if fg_exe == exe {
                    session.foreground_duration_seconds += elapsed;
                    log_debug!("Accumulated {}s foreground time for {}", elapsed, exe);
                }
            }
            
            session.last_update_time += chrono::Duration::seconds(elapsed as i64);
        }
    }

    /// Generates incremental DbSessions for currently running processes and resets their active timers.
    pub fn take_incremental_sessions(&mut self) -> Vec<DbSession> {
        let now = Utc::now();
        let mut incremental_sessions = Vec::new();
        
        for (_, session) in self.sessions.iter_mut() {
            let elapsed = now.signed_duration_since(session.last_update_time).num_seconds().max(0) as u64;
            let total = session.total_duration_seconds + elapsed;
            let mut fg = session.foreground_duration_seconds;
            if let Some(ref fg_exe) = self.focused_exe {
                if fg_exe == &session.exe_name {
                    fg += elapsed;
                }
            }
            
            if total > 0 || fg > 0 {
                let segments = split_session_by_days(
                    session.exe_name.clone(),
                    session.start_time,
                    now,
                    total,
                    fg,
                );
                incremental_sessions.extend(segments);
                
                // Reset accumulators for next increment
                session.start_time = now;
                session.total_duration_seconds = 0;
                session.foreground_duration_seconds = 0;
            }
            session.last_update_time = now;
        }
        
        incremental_sessions
    }

    /// Resets the last update time of all active sessions to the current time.
    pub fn reset_last_update_time(&mut self) {
        let now = Utc::now();
        for session in self.sessions.values_mut() {
            session.last_update_time = now;
        }
    }

    /// Returns a list of currently active applications and their running state
    pub fn get_active_apps(&self) -> Vec<(String, u64, u64)> {
        let now = Utc::now();
        self.sessions.iter().map(|(exe, s)| {
            let elapsed = now.signed_duration_since(s.last_update_time).num_seconds().max(0) as u64;
            let total = s.total_duration_seconds + elapsed;
            let mut fg = s.foreground_duration_seconds;
            if let Some(ref fg_exe) = self.focused_exe {
                if fg_exe == exe {
                    fg += elapsed;
                }
            }
            (exe.clone(), total, fg)
        }).collect()
    }
}
