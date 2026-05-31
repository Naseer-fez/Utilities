Create a highly optimized desktop Reminder Popup application using Rust.

The application must be:

* lightweight
* modular
* portable
* event-driven
* low RAM usage
* low CPU usage
* modern looking
* easy to extend later
* LLM-friendly codebase
* maintainable
* cleanly separated into modules

The application should run in the background and behave like a lightweight native reminder engine.

# Core Requirements

## Main Behavior

The app runs silently in the background.

When a reminder time is reached:

* a popup notification appears
* popup automatically closes after 5 seconds
* popup can display:

  * title
  * message
  * optional image

The app must support:

* one-time reminders
* repeating reminders
* customizable repeat counts
* reminder history stack

The app should be portable:

* no installation required
* runnable from USB/pendrive
* reminders stored locally in JSON files

---

# Architecture Requirements

Use a modular architecture.

Separate the codebase into clearly isolated modules.

The notification rendering system MUST be isolated in a separate module/file so it can later be redesigned independently without affecting scheduler logic.

Recommended folder structure:

reminder_app/
│
├── src/
│   ├── main.rs
│   ├── scheduler/
│   ├── notification/
│   ├── storage/
│   ├── ui/
│   ├── core/
│   └── utils/
│
├── reminders/
│   ├── reminders.json
│   ├── settings.json
│   └── images/
│
├── assets/
│
└── Cargo.toml

---

# Technology Stack

Use:

* Rust
* Slint UI framework

Do NOT use:

* Electron
* Tauri
* WebView-based systems
* heavy frameworks

Reason:
Need raw native performance, low RAM usage, fast startup, and portable executable support.

---

# Scheduler Design

The scheduler must be highly optimized.

DO NOT use:
while(true) polling loops checking reminders every second.

Instead:

* scheduler thread sleeps until the next reminder time
* wake only when needed
* use event-driven scheduling

Use:

* async channels
* tokio or crossbeam channels

The scheduler should:

* load reminders from JSON
* determine nearest reminder
* sleep until reminder time
* send event to notification system
* update repeat logic
* move completed reminders into history stack

---

# Threading Model

Use a clean threading model.

Main process:

* startup
* tray handling
* module initialization

Scheduler thread:

* manages timing
* waits efficiently
* dispatches reminder events

Notification thread:

* renders popup
* handles popup timeout
* handles image rendering
* auto closes after 5 seconds

Communication between systems should use channels/events.

Avoid unnecessary mutex-heavy architecture.

---

# Reminder Storage

Use JSON files for storage.

Example reminder format:

{
"id": 1,
"title": "Drink Water",
"message": "Take a break",
"time": "2026-05-30T19:30:00",
"repeat": {
"enabled": true,
"interval_minutes": 30,
"remaining": 5
},
"image": "images/water.png"
}

Requirements:

* human-readable JSON
* easy editing
* portable storage
* image paths stored relative to executable

---

# UI Requirements

UI should be:

* minimal
* modern
* smooth
* lightweight
* native feeling

Use:

* subtle rounded corners
* clean spacing
* smooth animations
* simple modern typography

Avoid:

* overly complex dashboard systems
* heavy animations
* unnecessary rendering loops

The popup notification should:

* fade in
* stay visible for 5 seconds
* fade out
* support optional image
* support future customization

---

# Notification Engine Requirements

Notification rendering MUST be isolated.

Example modules:

* popup.rs
* animation.rs
* renderer.rs

This separation is mandatory so the notification system can later be redesigned independently.

The notification system should support future extensibility:

* sound support
* buttons
* snooze
* transparency
* blur effects
* stacked notifications

---

# Performance Targets

Target:

* idle RAM usage under 25MB
* near-zero CPU usage while idle
* fast startup
* no constant polling
* low disk writes

Optimize for:

* event-driven architecture
* lazy loading
* minimal redraws
* low wake frequency

Do not preload unnecessary images into RAM.

Load images only when popup appears.

---

# Portability Requirements

The app should:

* work as standalone executable
* run without installation
* support moving entire folder to pendrive
* store all data locally

Build target:
cargo build --release

Expected distribution:

* app.exe
* reminders/
* assets/

No installer required.

---

# Code Quality Requirements

Code must be:

* production-style
* cleanly commented
* modular
* beginner-readable
* scalable
* easy for future LLMs to extend

Use:

* descriptive naming
* separated responsibilities
* reusable modules
* strongly typed structures

Avoid:

* giant files
* tightly coupled systems
* duplicated logic
* unnecessary abstractions

---

# Additional Features

Include:

* system tray support
* reminder history stack
* repeat reminder logic
* configurable popup duration
* image attachment support

Use VecDeque for reminder history queue.

---

# Final Goal

Build a highly optimized native desktop reminder popup engine with:

* minimal resource usage
* modern UI
* event-driven scheduling
* portable architecture
* isolated notification renderer
* clean modular Rust codebase
* future extensibility
* production-quality structure
