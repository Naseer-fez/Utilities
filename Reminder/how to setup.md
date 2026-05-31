# How to Set Up and Run the Reminder App

This document explains how to configure, run, compile, and customize the icons for the Reminder Popup App.

---

## 🛠️ Prerequisites

To build and run this project, you need:
1. **Rust Toolchain**: Install via [rustup](https://rustup.rs/).
2. **MinGW / GCC (for Windows GNU Toolchain)**:
   Since this project is configured to run on the GNU toolchain (`stable-x86_64-pc-windows-gnu`) to bypass the Microsoft Visual C++ Build Tools requirement, make sure a GCC compiler is available in your system path (e.g. from MSYS2, w64devkit, or Git Bash compiler tools).

If you haven't switched to the GNU toolchain yet, run the following in your command prompt:
```bash
rustup toolchain install stable-x86_64-pc-windows-gnu
rustup override set stable-x86_64-pc-windows-gnu
```

---

## 🎨 How to Change the Icons

The application uses three icons depending on the context:

### 1. The Executable (.exe) Icon
This is the icon shown in Windows Explorer and the taskbar.
* **Location**: `assets/icon.ico`
* **How to Change**: Replace `assets/icon.ico` with your own `.ico` file of the same name.
* **Mechanism**: Embedded into the binary at compile time via the `winres` dependency in `build.rs`.

### 2. The Window Icon
This is the icon displayed on the top-left title bar of the "Manage Reminders" GUI window.
* **Location**: `assets/icon.png`
* **How to Change**: Replace `assets/icon.png` with a PNG image of your choice.
* **Mechanism**: Loaded and compiled by Slint using `@image-url("../assets/icon.png")` in `ui/editor.slint`.

### 3. The System Tray Icon
This is the icon displayed in the Windows taskbar system tray (notification area).
* **Location**: `assets/icon.png`
* **How to Change**: Replace `assets/icon.png` with a 32x32 or 64x64 pixel PNG image.
* **Mechanism**: Loaded at startup by the background daemon in `src/main.rs`.

> [!TIP]
> After replacing any icon files, you must rebuild the project using `cargo build` or `cargo run` for the changes to take effect in the binary.

---

## 🚀 How to Run the Project

You can run the application in three different modes:

### 1. Run the Background Daemon (Default Mode)
This launches the scheduler and places a custom icon in your system tray. The daemon sleeps until a reminder is triggered.
```bash
cargo run
```

### 2. Run the Reminder Editor Directly
To launch the GUI window to add, edit, or delete reminders directly without starting the background daemon:
```bash
cargo run -- --editor
```

### 3. Manually Trigger a Popup Notification
To test a custom popup with specified text, run the following command:
```bash
cargo run -- --popup --title "Custom Title" --message "This is a custom alert!" --duration 5
```
You can also include an optional image:
```bash
cargo run -- --popup --title "Hydration" --message "Drink water!" --image "reminders/images/water.png"
```

---

## 📦 How to Build for Release

To build a standalone, lightweight, size-optimized executable:
```bash
cargo build --release
```
The resulting executable will be saved at `target/release/reminder-app.exe`.

### Portability Setup
To distribute the application (e.g., on a USB/pendrive), package the following files in the same directory:
```
my_reminder_folder/
├── reminder-app.exe      (from target/release/reminder-app.exe)
├── assets/
│   └── icon.png          (needed for system tray icon)
└── reminders/            (automatically created on first run if missing)
    ├── reminders.json
    └── settings.json
```
No installation is required. Just run `reminder-app.exe`.
