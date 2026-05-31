# Build & Test Guide: Live Wallpaper Engine

This document provides a step-by-step guide on how to build, run, configure, and test the **Windows Live Wallpaper Engine**.

---

## 🛠️ Prerequisites

Ensure you have the following installed on your Windows system:
* **Operating System:** Windows 10 or Windows 11.
* **Compiler:** MSVC (Visual Studio 2019 / 2022) with C++17 support.
* **Build System:** [CMake](https://cmake.org/download/) (v3.20 or higher).
* **Generator (Optional but recommended):** Ninja (for fast command-line builds) or Visual Studio Solutions.

---

## 🏗️ 1. Building the Application

### Option A: Using Command Line (CMake & Ninja)
Since the project is already configured with Ninja in your `build` directory, you can build it with a single command from the project root:

```powershell
# Compile the project
cmake --build build
```

If you want to configure and build from scratch:
```powershell
# 1. Clean/remove the old build directory if necessary
# 2. Create and enter a new build folder
mkdir build_new
cd build_new

# 3. Configure using CMake (Release build)
cmake -DCMAKE_BUILD_TYPE=Release ..

# 4. Compile the executable
cmake --build . --config Release
```

The output executable `LiveWallpaper.exe` will be built under the `build/` (or `build/Release/`) folder.

---

### Option B: Using Visual Studio (GUI)
1. Launch **Visual Studio**.
2. Select **Open a local folder** and browse to `d:\CODE\Utlities\LiveWallpaper`.
3. Visual Studio will automatically detect `CMakeLists.txt` and generate the CMake cache.
4. Select `LiveWallpaper.exe` as the startup target from the toolbar dropdown.
5. Press **Ctrl+F5** to build and run the release version, or **F5** to build and run in Debug mode.

---

## 🏃 2. Running & Configuring

### Launching the Application
Double-click `LiveWallpaper.exe` to run the engine.
* **System Tray:** The app runs in the background. Look for the screen/monitor icon in the system tray (bottom-right taskbar, near the clock).
* **Fallback Video:** On the very first run, if no video path is saved in the configuration, the application will default to looking for a video in:
  `C:\Users\FEZ NASEER\Videos\Captures\FIFA 23 2026-03-03 17-44-16.mp4`

### Configuration and Logs
Configuration settings and runtime logs are stored in your user profile:
* **Configuration File:** `%APPDATA%\LiveWallpaper\config.ini`  
  *(Resolves to: `C:\Users\<YourUsername>\AppData\Roaming\LiveWallpaper\config.ini`)*
* **Log File:** `%APPDATA%\LiveWallpaper\log.txt`  
  *(Resolves to: `C:\Users\<YourUsername>\AppData\Roaming\LiveWallpaper\log.txt`)*

#### Example `config.ini`
```ini
[Settings]
VideoPath=C:\Users\FEZ NASEER\Videos\MyWallpaper.mp4
Playlist=C:\Users\FEZ NASEER\Videos\MyWallpaper.mp4|C:\Users\FEZ NASEER\Videos\AnotherVideo.mp4
Paused=0
RotationInterval=10
IdleTimeout=5
```

---

## 🧪 3. How to Test

Here is a testing checklist to ensure the engine is fully operational:

### Test Case 1: Core Rendering & Playback
1. Run the application.
2. Verify that a video is playing behind your desktop icons.
3. Check the log file `%APPDATA%\LiveWallpaper\log.txt` to verify there are no startup errors.

### Test Case 2: System Tray & UI Controls
1. Right-click the **Live Wallpaper System Tray Icon**.
2. Click **Play / Pause** and verify that the wallpaper pauses/resumes rendering.
3. Click **Add Video...** and select a `.mp4` file. Verify that the wallpaper updates to the selected video.
4. Click **Manage Playlist...** to open the playlist manager dialog.
   * Add multiple videos to the playlist.
   * Re-order items.
   * Click **Save** and verify the active wallpaper updates to the selected active video.
5. Click **Clear Playlist** and verify that wallpaper rendering stops (the screen returns to your static Windows wallpaper).

### Test Case 3: Auto-Pause (Performance & Focus Monitoring)
The wallpaper should automatically pause when a full-screen application or game is active to save resources.
1. Open a web browser or Notepad and press **F11** to make it full-screen.
2. Open `%APPDATA%\LiveWallpaper\log.txt` and look for a line like:
   `Power state changed. Effective pause state: 1`
3. Restore the window (exit full-screen) and verify the logs show the state resuming:
   `Power state changed. Effective pause state: 0`

### Test Case 4: Explorer Crash / Watchdog Recovery
The engine features an auto-recovery watchdog that re-injects the live wallpaper when Windows Explorer crashes or restarts.
1. Open **Task Manager** (`Ctrl+Shift+Esc`).
2. Locate **Windows Explorer** in the processes list.
3. Right-click it and choose **Restart** (or end the process and start a new `explorer.exe` task).
4. Verify that:
   * The desktop desktop icons reload.
   * The live wallpaper is restored behind the icons automatically within a few seconds without restarting the application.
   * The log file records the recovery:
     `Host window recreated! Requesting RenderEngine recovery on new handle.`

### Test Case 5: Rotation Timer
1. Right-click the tray icon and navigate to **Rotation Interval**.
2. Select **1 Minute** (or set `RotationInterval=1` in `config.ini` and restart).
3. Wait for over a minute and verify that the wallpaper automatically transitions to the next video in your playlist.

### Test Case 6: Idle Detection
The wallpaper pauses when the computer goes idle.
1. Set the idle timeout to **1 minute** by editing `IdleTimeout=1` in `config.ini`.
2. Do not touch your mouse or keyboard for 60 seconds.
3. Check the logs (`%APPDATA%\LiveWallpaper\log.txt`) to verify that the wallpaper paused automatically when the system entered the idle state.
4. Move your mouse and verify playback resumes instantly.
