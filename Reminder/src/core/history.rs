// src/core/history.rs - Tracks and persists past triggered reminder history using a VecDeque

use serde::{Deserialize, Serialize};
use std::collections::VecDeque;
use std::fs;
use crate::utils::resolve_path;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HistoryItem {
    pub id: String,
    pub title: String,
    pub message: String,
    pub triggered_at: String,
}

pub struct HistoryManager {
    history: VecDeque<HistoryItem>,
    max_size: usize,
}

impl HistoryManager {
    /// Loads history from `reminders/history.json` or creates a new queue.
    pub fn new(max_size: usize) -> Self {
        let history_path = resolve_path("reminders/history.json");
        
        let history = if history_path.exists() {
            match fs::read_to_string(&history_path) {
                Ok(content) => serde_json::from_str::<VecDeque<HistoryItem>>(&content)
                    .unwrap_or_else(|e| {
                        log::error!("Failed to parse history.json: {}. Creating new queue.", e);
                        VecDeque::new()
                    }),
                Err(e) => {
                    log::error!("Failed to read history.json: {}. Creating new queue.", e);
                    VecDeque::new()
                }
            }
        } else {
            VecDeque::new()
        };

        let mut manager = Self { history, max_size };
        manager.enforce_limit();
        manager
    }

    /// Adds a new triggered reminder to the history queue and saves it.
    pub fn add(&mut self, id: String, title: String, message: String) {
        let now_str = chrono::Local::now().format("%Y-%m-%dT%H:%M:%S").to_string();
        
        let item = HistoryItem {
            id,
            title,
            message,
            triggered_at: now_str,
        };

        self.history.push_front(item);
        self.enforce_limit();

        if let Err(e) = self.save() {
            log::error!("Failed to save history: {}", e);
        }
    }

    /// Retrieves all items currently in history.
    pub fn get_all(&self) -> Vec<HistoryItem> {
        self.history.iter().cloned().collect()
    }

    /// Keeps history queue capped under max_size limit.
    fn enforce_limit(&mut self) {
        while self.history.len() > self.max_size {
            self.history.pop_back();
        }
    }

    /// Saves the current history to `reminders/history.json`.
    fn save(&self) -> Result<(), Box<dyn std::error::Error>> {
        let history_path = resolve_path("reminders/history.json");

        if let Some(parent) = history_path.parent() {
            fs::create_dir_all(parent)?;
        }

        let serialized = serde_json::to_string_pretty(&self.history)?;
        fs::write(history_path, serialized)?;
        Ok(())
    }
}
