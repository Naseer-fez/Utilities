# Ultra-Lightweight Windows Executable Habit Tracker — Full Project Specification

## Project Goal

Build an ultra-optimized Windows background application that tracks usage time of selected `.exe` applications with minimal RAM and CPU usage.

The application must:

* Run silently in the background
* Track selected executables only
* Store all data locally
* Use extremely low system resources
* Be modular and expandable
* Use efficient native Windows APIs
* Be optimized specifically for:

  * Intel i5-12450HX
  * 16GB RAM
  * Windows 10/11

This is NOT spyware.
This project only tracks executable runtime and focused usage duration.

---

# Core Requirements

## Main Features

The application should:

* Allow user to select specific `.exe` files to track
* Detect:

  * app opened
  * app closed
  * foreground/focused usage time
  * total runtime
* Ignore all untracked executables
* Store sessions locally in SQLite
* Save data safely during shutdown/restart/logoff
* Run with extremely low RAM and CPU usage
* Use adaptive polling
* Be highly modular for future extensions

---

# Technology Stack

## Core Language

Use:

* Rust

Reason:

* memory safety
* low RAM usage
* stable long-running background services
* near-C++ performance

---

## Windows Integration

Use:

* `windows-rs`
* WinAPI directly where needed

Avoid:

* Electron
* Chromium
* heavy frameworks

---

## Database

Use:

* SQLite
* WAL mode enabled

Required SQLite pragmas:

```sql
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
PRAGMA temp_store=MEMORY;
```

---

## Async Runtime

Use:

* Tokio

---

## UI

Simple lightweight tray UI only.

No heavy dashboard initially.

UI should include:

* Start/Stop tracking
* Add/remove tracked executables
* View daily stats
* Open database folder
* Exit app safely

Avoid:

* React
* Electron
* WebView-heavy architecture

Preferred:

* Native tray
* Tauri only if lightweight enough

---

# Tracking Architecture

## IMPORTANT

DO NOT continuously scan all processes every few seconds.

That is inefficient.

Use hybrid architecture instead.

---

# Tracking Method

## 1. Process Start/Stop Detection

Use event-driven monitoring.

Preferred:

* WMI process events
  OR
* Win32 process notifications

Must detect:

* process started
* process ended

Only monitor tracked executables.

---

## 2. Foreground Window Tracking

Use lightweight polling.

Poll interval:

* Active usage:

  * every 1000ms
* Idle state:

  * every 5000ms

Use:

```cpp
GetForegroundWindow()
GetWindowThreadProcessId()
```

---

## 3. Idle Detection

Use:

```cpp
GetLastInputInfo()
```

If user idle:

* reduce polling frequency
* reduce CPU wakeups

---

# Usage Logic

Track BOTH:

## A. Total Runtime

Time executable exists.

## B. Foreground Runtime

Time executable is focused and active.

Store separately.

This is mandatory.

---

# Data Storage Rules

## NEVER WRITE EVERY SECOND

Bad architecture.

Instead:

* Maintain in-memory session cache
* Flush to SQLite:

  * every 30 minutes
  * on app close
  * on shutdown
  * on restart
  * on sleep event

---

# Session-Based Storage

DO NOT store raw polling logs.

BAD:

```txt
chrome active at 1:01
chrome active at 1:02
chrome active at 1:03
```

GOOD:

```txt
chrome.exe
start_time
end_time
foreground_duration
total_duration
```

Only store finalized sessions.

---

# Shutdown Safety

Application MUST safely handle:

* shutdown
* restart
* user logoff
* sleep

Use:

* `WM_QUERYENDSESSION`
* `WM_ENDSESSION`

Before shutdown:

* flush session cache
* commit SQLite transaction
* close database safely

---

# Performance Requirements

## CPU Usage

Target:

* 0%–0.2% idle CPU usage

---

## RAM Usage

Target:

* under 30MB RAM
* ideal:

  * 8MB–20MB

---

## Disk Writes

Minimize writes aggressively.

Use:

* batching
* transactions
* session aggregation

---

# Modularity Requirements

Design project as independent modules.

Required folder structure:

```txt
/modules
  tracker_core
  process_events
  foreground_tracker
  idle_detector
  sqlite_logger
  session_cache
  analytics
  tray_ui
  config_manager
```

Each module must:

* have isolated responsibility
* avoid tight coupling
* support future expansion

---

# Config System

Use lightweight config system.

Support:

* add/remove executables
* enable/disable tracking
* categories/tags
* polling intervals
* startup options

Config should persist locally.

Preferred:

* TOML
  OR
* JSON

---

# Database Schema

## tracked_apps

```sql
CREATE TABLE tracked_apps (
    id INTEGER PRIMARY KEY,
    exe_name TEXT UNIQUE NOT NULL,
    enabled INTEGER DEFAULT 1,
    category TEXT
);
```

---

## usage_sessions

```sql
CREATE TABLE usage_sessions (
    id INTEGER PRIMARY KEY,
    exe_name TEXT NOT NULL,
    start_time INTEGER NOT NULL,
    end_time INTEGER NOT NULL,
    total_duration_seconds INTEGER NOT NULL,
    foreground_duration_seconds INTEGER NOT NULL,
    date TEXT NOT NULL
);
```

---

# Adaptive Polling System

Required behavior:

## User Active

Poll foreground window every:

```txt
1000ms
```

---

## User Idle

Poll every:

```txt
5000ms
```

---

## System Sleep

Suspend unnecessary tracking threads.

Resume safely after wake.

---

# Threading Architecture

Use lightweight thread model.

Recommended structure:

```txt
Main Thread
 ├── Tray UI
 ├── Process Event Listener
 ├── Foreground Tracker
 ├── Idle Detector
 ├── SQLite Flush Worker
 └── Analytics Worker
```

Avoid:

* excessive thread spawning
* busy loops
* high-frequency timers

---

# Session Cache

Maintain active sessions in memory.

Preferred structure:

```rust
HashMap<String, ActiveSession>
```

This should:

* reduce database writes
* reduce SSD wear
* improve responsiveness

---

# Error Handling

Application must:

* never crash silently
* auto-recover from SQLite locks
* gracefully recover from process event failures
* handle corrupted config safely

Use structured logging.

Preferred:

* `tracing`

---

# Startup Behavior

Support:

* auto-start with Windows
* delayed startup option
* silent background launch

---

# Future Expandability

Architecture must allow future modules such as:

* productivity analytics
* streak systems
* charts
* app categories
* focus sessions
* notifications
* exports
* cloud sync
* automation triggers

Design now for scalability.

---

# Important Constraints

DO NOT:

* use Electron
* use Chromium-heavy UI
* scan all processes repeatedly
* write to DB every second
* track keyboard input
* track files
* track browser history
* use cloud dependency
* create spyware behavior

---

# Development Priorities

Priority order:

1. Efficiency
2. Stability
3. Low RAM usage
4. Low CPU usage
5. Accurate session tracking
6. Modular architecture
7. Expandability
8. UI polish

---

# Deliverables

Build:

* complete Rust project
* modular architecture
* SQLite integration
* tray application
* optimized tracking engine
* shutdown-safe persistence
* adaptive polling system
* configuration system
* executable tracking management

Code should be:

* production-quality
* heavily optimized
* maintainable
* scalable
* well-commented
* benchmark-conscious

Focus on:

* native performance
* low wakeups
* low allocations
* efficient memory usage
* clean architecture
* long-term maintainability
