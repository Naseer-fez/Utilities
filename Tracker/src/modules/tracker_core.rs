use std::collections::HashSet;
use std::sync::{Arc, Mutex};
use std::time::{Instant, Duration};
use std::sync::mpsc;
use crate::{log_info, log_warn, log_error, log_debug};

use super::config_manager::ConfigManager;
use super::sqlite_logger::SqliteLogger;
use super::session_cache::SessionCache;
use super::idle_detector::IdleDetector;
use super::foreground_tracker::ForegroundTracker;
use super::process_events::{ProcessEventsObserver, ProcessEvent};
use super::shutdown_handler::{ShutdownHandler, ShutdownMessage};
use super::tray_ui::{TrayUi, TrayCommand};
use super::analytics::Analytics;

enum Event {
    Process(ProcessEvent),
    Foreground(Option<String>),
    Tray(TrayCommand),
    Shutdown(ShutdownMessage),
    Tick,
}

pub struct TrackerCore {
    config_manager: Arc<Mutex<ConfigManager>>,
    sqlite_logger: Arc<SqliteLogger>,
    session_cache: Arc<Mutex<SessionCache>>,
    idle_detector: Arc<IdleDetector>,
    analytics: Arc<Analytics>,
    
    // Tracked apps shared list for process events observer
    tracked_apps_set: Arc<Mutex<HashSet<String>>>,
    
    // Communication channels
    process_rx: Option<mpsc::Receiver<ProcessEvent>>,
    foreground_rx: Option<mpsc::Receiver<Option<String>>>,
    shutdown_rx: Option<mpsc::Receiver<ShutdownMessage>>,
    tray_rx: Option<mpsc::Receiver<TrayCommand>>,
    
    // Channels to pass senders
    process_tx: mpsc::Sender<ProcessEvent>,
    foreground_tx: mpsc::Sender<Option<String>>,
    shutdown_tx: mpsc::Sender<ShutdownMessage>,
    tray_tx: mpsc::Sender<TrayCommand>,
}

impl TrackerCore {
    pub fn new() -> Self {
        let config_manager = Arc::new(Mutex::new(ConfigManager::new()));
        let sqlite_logger = Arc::new(SqliteLogger::new());
        let session_cache = Arc::new(Mutex::new(SessionCache::new()));
        
        let idle_threshold = {
            let config = config_manager.lock().unwrap();
            config.config.idle_threshold_secs
        };
        let idle_detector = Arc::new(IdleDetector::new(idle_threshold));
        let analytics = Arc::new(Analytics::new(sqlite_logger.clone()));

        // Create tracked apps set for filtering in process observer
        let tracked_apps_set = Arc::new(Mutex::new(HashSet::new()));
        Self::sync_tracked_apps_set(&config_manager, &tracked_apps_set, &sqlite_logger);

        // Define communication channels
        let (process_tx, process_rx) = mpsc::channel();
        let (foreground_tx, foreground_rx) = mpsc::channel();
        let (shutdown_tx, shutdown_rx) = mpsc::channel();
        let (tray_tx, tray_rx) = mpsc::channel();

        Self {
            config_manager,
            sqlite_logger,
            session_cache,
            idle_detector,
            analytics,
            tracked_apps_set,
            process_rx: Some(process_rx),
            foreground_rx: Some(foreground_rx),
            shutdown_rx: Some(shutdown_rx),
            tray_rx: Some(tray_rx),
            process_tx,
            foreground_tx,
            shutdown_tx,
            tray_tx,
        }
    }

    /// Synchronizes the tracked apps list in memory and DB from config
    fn sync_tracked_apps_set(
        config_manager: &Arc<Mutex<ConfigManager>>,
        tracked_apps_set: &Arc<Mutex<HashSet<String>>>,
        sqlite_logger: &Arc<SqliteLogger>,
    ) {
        let config = config_manager.lock().unwrap();
        let mut set = tracked_apps_set.lock().unwrap();
        set.clear();
        
        for app in &config.config.tracked_apps {
            if app.enabled {
                set.insert(app.exe_name.to_lowercase());
            }
        }
        
        // Sync database table
        if let Err(e) = sqlite_logger.sync_tracked_apps(&config.config.tracked_apps) {
            log_error!("Failed to sync tracked apps table: {}", e);
        }
    }

    /// Clean up ghost sessions that are no longer running on the OS
    pub fn clean_ghost_sessions(&self) {
        let running_processes = ProcessEventsObserver::get_all_running_processes();
        let mut cache = self.session_cache.lock().unwrap();
        
        let mut dead_exes = Vec::new();
        for exe in cache.sessions.keys() {
            if !running_processes.contains(exe) {
                dead_exes.push(exe.clone());
            }
        }
        
        let mut finalized = Vec::new();
        for exe in dead_exes {
            log_warn!("Cleaning up ghost session for non-running application: {}", exe);
            if let Some(mut sessions) = cache.process_stopped_internal(&exe) {
                finalized.append(&mut sessions);
            }
        }
        
        if !finalized.is_empty() {
            let _ = self.sqlite_logger.insert_sessions(&finalized);
        }
    }

    /// Starts all tracking tasks and orchestrates the central event loop
    pub fn start(mut self) {
        log_info!("Initializing tracker core services...");
        
        // Instantiate ShutdownHandler (hidden window registers automatically)
        let _shutdown_handler = ShutdownHandler::new(self.shutdown_tx.clone());

        // Get config parameters
        let (tracking_enabled, auto_start, active_poll, idle_poll, flush_interval_mins) = {
            let c = self.config_manager.lock().unwrap();
            (
                c.config.tracking_enabled,
                c.config.auto_start,
                c.config.active_poll_interval_ms,
                c.config.idle_poll_interval_ms,
                c.config.db_flush_interval_mins,
            )
        };

        // Instantiate and start Tray UI
        let (tray_ui, update_rx) = TrayUi::new(self.tray_tx.clone());
        self.update_tray(&tray_ui);
        
        let tracked_apps_set_clone = self.tracked_apps_set.clone();
        let process_observer = ProcessEventsObserver::new(self.process_tx.clone(), tracked_apps_set_clone);

        // Always run foreground and process event observers
        let foreground_tracker = ForegroundTracker::new(
            self.idle_detector.clone(),
            self.foreground_tx.clone(),
            active_poll,
            idle_poll,
        );
        std::thread::spawn(move || foreground_tracker.run());
        std::thread::spawn(move || process_observer.run());

        let active_sessions = self.session_cache.lock().unwrap().get_active_apps();
        let tracked_list = self.get_tracked_apps_list();
        let today_stats = self.analytics.get_today_stats_formatted(active_sessions, &tracked_list);

        tray_ui.start(
            tracking_enabled,
            auto_start,
            tracked_list,
            today_stats,
            update_rx,
        );

        // Run ghost session cleanup initially
        self.clean_ghost_sessions();

        // Build our multiplexer event queue
        let (event_tx, event_rx) = mpsc::channel::<Event>();

        // 1. Process events forwarder
        let process_rx = self.process_rx.take().unwrap();
        let process_tx = event_tx.clone();
        std::thread::spawn(move || {
            while let Ok(msg) = process_rx.recv() {
                if process_tx.send(Event::Process(msg)).is_err() {
                    break;
                }
            }
        });

        // 2. Foreground events forwarder
        let foreground_rx = self.foreground_rx.take().unwrap();
        let foreground_tx = event_tx.clone();
        std::thread::spawn(move || {
            while let Ok(msg) = foreground_rx.recv() {
                if foreground_tx.send(Event::Foreground(msg)).is_err() {
                    break;
                }
            }
        });

        // 3. Tray events forwarder
        let tray_rx = self.tray_rx.take().unwrap();
        let tray_tx = event_tx.clone();
        std::thread::spawn(move || {
            while let Ok(msg) = tray_rx.recv() {
                if tray_tx.send(Event::Tray(msg)).is_err() {
                    break;
                }
            }
        });

        // 4. Shutdown events forwarder
        let shutdown_rx = self.shutdown_rx.take().unwrap();
        let shutdown_tx = event_tx.clone();
        std::thread::spawn(move || {
            while let Ok(msg) = shutdown_rx.recv() {
                if shutdown_tx.send(Event::Shutdown(msg)).is_err() {
                    break;
                }
            }
        });

        // 5. 1-second timer thread for ticks
        let tick_tx = event_tx.clone();
        std::thread::spawn(move || {
            loop {
                std::thread::sleep(Duration::from_secs(1));
                if tick_tx.send(Event::Tick).is_err() {
                    break;
                }
            }
        });

        let mut last_tick = Instant::now();
        let mut last_flush = Instant::now();
        let mut focused_exe: Option<String> = None;

        log_info!("Tracker core loop active.");

        // Central Event Loop (Blocks on recv, CPU usage literally 0.00% when idle)
        while let Ok(event) = event_rx.recv() {
            match event {
                Event::Process(proc_event) => {
                    match proc_event {
                        ProcessEvent::Started(exe) => {
                            self.session_cache.lock().unwrap().process_started(&exe);
                        }
                        ProcessEvent::Stopped(exe) => {
                            let finalized = self.session_cache.lock().unwrap().process_stopped(&exe);
                            if !finalized.is_empty() {
                                let _ = self.sqlite_logger.insert_sessions(&finalized);
                                self.update_tray(&tray_ui);
                            }
                        }
                    }
                }
                Event::Foreground(app_opt) => {
                    focused_exe = app_opt;
                    self.session_cache.lock().unwrap().set_focused_exe(focused_exe.as_deref());
                }
                Event::Tray(cmd) => {
                    match cmd {
                        TrayCommand::ToggleTracking => {
                            self.handle_toggle_tracking(&tray_ui);
                        }
                        TrayCommand::ToggleAutoStart => {
                            self.handle_toggle_autostart(&tray_ui);
                        }
                        TrayCommand::AddApp(exe_path) => {
                            self.handle_add_app(&tray_ui, exe_path);
                        }
                        TrayCommand::RemoveApp(exe_name) => {
                            self.handle_remove_app(&tray_ui, &exe_name);
                        }
                        TrayCommand::OpenDataFolder => {
                            let db_path = self.sqlite_logger.get_db_path();
                            if let Some(parent) = db_path.parent() {
                                std::process::Command::new("explorer")
                                    .arg(parent)
                                    .spawn()
                                    .ok();
                            }
                        }
                        TrayCommand::ShowReport => {
                            let active_sessions = self.session_cache.lock().unwrap().get_active_apps();
                            let tracked_list = self.get_tracked_apps_list();
                            let html = self.analytics.generate_report_html(active_sessions, &tracked_list);
                            
                            let app_data = std::env::var("APPDATA").unwrap_or_else(|_| ".".to_string());
                            let report_dir = std::path::PathBuf::from(&app_data).join("ExeTracker");
                            if !report_dir.exists() {
                                let _ = std::fs::create_dir_all(&report_dir);
                            }
                            let report_path = report_dir.join("report.html");
                            if std::fs::write(&report_path, html).is_ok() {
                                log_info!("Generated report at: {:?}", report_path);
                                std::process::Command::new("cmd")
                                    .args(["/C", "start", "", &report_path.to_string_lossy()])
                                    .spawn()
                                    .ok();
                            } else {
                                log_error!("Failed to write HTML report to {:?}", report_path);
                            }
                        }
                        TrayCommand::Exit => {
                            log_info!("Exit command received from tray. Initiating graceful shutdown...");
                            self.graceful_shutdown();
                            std::process::exit(0);
                        }
                    }
                }
                Event::Shutdown(shutdown_msg) => {
                    match shutdown_msg {
                        ShutdownMessage::Shutdown(done_tx) => {
                            log_warn!("System shutting down! Performing urgent database flush.");
                            self.graceful_shutdown();
                            let _ = done_tx.send(()); // Signal to shutdown handler thread that flush is complete
                            std::process::exit(0);
                        }
                        ShutdownMessage::Suspend => {
                            log_warn!("System sleep detected! Flushing active sessions...");
                            self.flush_incremental_data();
                        }
                        ShutdownMessage::Resume => {
                            log_info!("System resume detected! Re-starting active tracking timers.");
                            last_tick = Instant::now();
                            self.session_cache.lock().unwrap().reset_last_update_time();
                        }
                    }
                }
                Event::Tick => {
                    let elapsed_tick = last_tick.elapsed();
                    last_tick = Instant::now();
                    if elapsed_tick > Duration::from_secs(10) {
                        log_warn!("Time jump detected ({:?})! Resetting session update times to prevent invalid accumulation.", elapsed_tick);
                        self.session_cache.lock().unwrap().reset_last_update_time();
                    } else {
                        let tracking_active = { self.config_manager.lock().unwrap().config.tracking_enabled };
                        if tracking_active {
                            let focused_ref = focused_exe.as_deref();
                            self.session_cache.lock().unwrap().tick(focused_ref);
                        }
                    }

                    // Periodic database flush check
                    if last_flush.elapsed() >= Duration::from_secs(flush_interval_mins * 60) {
                        last_flush = Instant::now();
                        log_debug!("Triggering periodic database flush");
                        self.clean_ghost_sessions();
                        self.flush_incremental_data();
                        self.update_tray(&tray_ui);
                    }
                }
            }
        }
    }

    /// Handles toggling active tracking state
    fn handle_toggle_tracking(&self, tray_ui: &TrayUi) {
        let mut config = self.config_manager.lock().unwrap();
        let new_state = !config.config.tracking_enabled;
        config.set_tracking_enabled(new_state);
        
        log_info!("Tracking state toggled: {}", if new_state { "ENABLED" } else { "PAUSED" });
        
        if !new_state {
            // When pausing, instantly flush all active tracking sessions
            let mut cache = self.session_cache.lock().unwrap();
            let sessions = cache.take_incremental_sessions();
            if let Err(e) = self.sqlite_logger.insert_sessions(&sessions) {
                log_error!("Failed to flush sessions on pause: {}", e);
            }
        }
        
        drop(config);
        
        // Re-initialize/re-boot tracking tasks or update tray icon
        self.update_tray(tray_ui);
    }

    /// Handles toggling windows auto-start
    fn handle_toggle_autostart(&self, tray_ui: &TrayUi) {
        let mut config = self.config_manager.lock().unwrap();
        let new_state = !config.config.auto_start;
        config.set_auto_start(new_state);
        drop(config);
        self.update_tray(tray_ui);
    }

    /// Handles adding a new app to track
    fn handle_add_app(&self, tray_ui: &TrayUi, exe_name: String) {
        let mut config = self.config_manager.lock().unwrap();
        if config.add_tracked_app(exe_name.clone(), Some("User Added".to_string())) {
            log_info!("Added tracked application: {}", exe_name);
            drop(config);
            Self::sync_tracked_apps_set(&self.config_manager, &self.tracked_apps_set, &self.sqlite_logger);
            
            // Instantly start tracking the new app if it is currently running
            let running = ProcessEventsObserver::get_all_running_processes();
            let exe_lower = exe_name.to_lowercase();
            if running.contains(&exe_lower) {
                self.session_cache.lock().unwrap().process_started(&exe_lower);
            }
            
            self.update_tray(tray_ui);
        }
    }

    /// Handles removing a tracked app
    fn handle_remove_app(&self, tray_ui: &TrayUi, exe_name: &str) {
        let mut config = self.config_manager.lock().unwrap();
        if config.remove_tracked_app(exe_name) {
            log_info!("Removed tracked application: {}", exe_name);
            drop(config);
            Self::sync_tracked_apps_set(&self.config_manager, &self.tracked_apps_set, &self.sqlite_logger);
            
            // If the application was active, stop tracking it and flush
            let exe_lower = exe_name.to_lowercase();
            let finalized = self.session_cache.lock().unwrap().process_stopped(&exe_lower);
            if !finalized.is_empty() {
                let _ = self.sqlite_logger.insert_sessions(&finalized);
            }
            
            self.update_tray(tray_ui);
        }
    }

    /// Flushes all incremental active session data to SQLite
    fn flush_incremental_data(&self) {
        let mut cache = self.session_cache.lock().unwrap();
        let sessions = cache.take_incremental_sessions();
        if !sessions.is_empty() {
            if let Err(e) = self.sqlite_logger.insert_sessions(&sessions) {
                log_error!("Failed to flush incremental session data: {}", e);
            }
        }
    }

    /// Graceful shutdown flushes all sessions to the database
    fn graceful_shutdown(&self) {
        log_info!("Committing session data cache and closing database...");
        self.flush_incremental_data();
        log_info!("Graceful shutdown complete.");
    }

    /// Triggers Tray UI rebuild
    fn update_tray(&self, tray_ui: &TrayUi) {
        let config = self.config_manager.lock().unwrap();
        let tracking_enabled = config.config.tracking_enabled;
        let auto_start = config.config.auto_start;
        
        let tracked_apps: Vec<String> = config.config.tracked_apps.iter()
            .map(|a| a.exe_name.clone())
            .collect();
            
        drop(config);

        let active_sessions = self.session_cache.lock().unwrap().get_active_apps();
        let today_stats = self.analytics.get_today_stats_formatted(active_sessions, &tracked_apps);
        
        tray_ui.update(tracking_enabled, auto_start, tracked_apps, today_stats);
    }

    /// Collects list of tracked app names
    fn get_tracked_apps_list(&self) -> Vec<String> {
        let config = self.config_manager.lock().unwrap();
        config.config.tracked_apps.iter()
            .map(|a| a.exe_name.clone())
            .collect()
    }
}
