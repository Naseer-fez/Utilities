# LiveWallpaper

**Ultra-optimized Windows live wallpaper manager** — render looping MP4 videos behind your desktop icons with minimal CPU/GPU overhead, using hardware-accelerated H.264 decoding and Direct3D 11.

---

## Features

- **Hardware-Accelerated Decoding** — H.264 video decoded via Media Foundation DXVA2, offloading work to the GPU
- **Direct3D 11 Rendering** — frames composited directly onto the desktop's WorkerW window with zero-copy GPU textures
- **Intelligent Power Management** — automatically pauses on battery power, during fullscreen applications, and after user idle timeout
- **Near-Zero Overhead** — integrated GPU preference by default, frame rate capping, and no rendering when the desktop is occluded
- **System Tray Integration** — pause/resume, change wallpaper, and configure settings from the notification area
- **Zero External Dependencies** — pure Win32/COM/DirectX, no third-party libraries, no runtime dependencies beyond Windows itself
- **Seamless Looping** — videos loop with automatic seek-back, no visible gap or flicker
- **Simple Configuration** — TOML-based config file in `%APPDATA%\LiveWallpaper\`

## System Requirements

| Requirement     | Minimum                                      |
|-----------------|----------------------------------------------|
| **OS**          | Windows 10 1809+ / Windows 11               |
| **GPU**         | Any GPU with D3D 11.0 and H.264 decode support (Intel HD 4000+, NVIDIA Kepler+, AMD GCN+) |
| **CPU**         | x64 processor                                |
| **RAM**         | 50 MB (typical working set)                  |
| **Disk**        | < 1 MB (application binary)                 |
| **Video Codec** | H.264/AVC in MP4 container                  |

## Build Instructions

### Prerequisites

- [CMake 3.20+](https://cmake.org/download/)
- [Visual Studio 2019+](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload
- Windows SDK 10.0.19041.0 or later

### Build Steps

```powershell
# Clone the repository
git clone https://github.com/your-username/LiveWallpaper.git
cd LiveWallpaper

# Configure (Release build)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release

# The binary is at build\Release\LiveWallpaper.exe
```

### Debug Build

```powershell
cmake --build build --config Debug
# Debug binary includes verbose logging to %APPDATA%\LiveWallpaper\log.txt
```

## Usage

### Quick Start

1. **Build** the project (see above)
2. **Run** `LiveWallpaper.exe`
3. **Right-click** the system tray icon → **Change Wallpaper...**
4. **Select** an MP4 file (H.264 encoded)
5. The video will begin playing behind your desktop icons

### System Tray Menu

| Menu Item         | Action                                    |
|-------------------|-------------------------------------------|
| **Pause**         | Freeze the current frame                  |
| **Resume**        | Resume video playback                     |
| **Change...**     | Open file picker to select a new MP4      |
| **Settings...**   | Open the settings dialog                  |
| **Exit**          | Gracefully shut down and restore wallpaper|

### Command-Line Options

```
LiveWallpaper.exe                     # Normal startup
LiveWallpaper.exe "C:\path\video.mp4" # Start with a specific video
```

## Configuration

Configuration is stored at `%APPDATA%\LiveWallpaper\config.toml`. A default template is provided in `config/default_config.toml`.

### Reference

```toml
[wallpaper]
path = "C:\\Videos\\my_wallpaper.mp4"   # Path to MP4 file (H.264)
fps = 24                                 # Target frame rate (1-60)

[behavior]
idle_timeout_minutes = 5                 # Freeze after N minutes idle (0 = disabled)
pause_on_battery = true                  # Auto-pause on battery power
pause_on_fullscreen = true               # Auto-pause when fullscreen app detected

[advanced]
gpu_preference = "integrated"            # "integrated", "discrete", or "auto"
```

### Configuration Notes

- Changes to `fps` take effect immediately on Apply
- `gpu_preference` changes require a restart
- The config file is created automatically on first launch if it doesn't exist
- Invalid values silently fall back to defaults

## Architecture Overview

```
┌──────────────────────────────────────────────────────┐
│                    main.cpp                          │
│           WinMain, message loop, tray icon           │
└──────────────┬───────────────────────────────────────┘
               │
    ┌──────────▼──────────┐
    │   WallpaperHost     │  Finds/creates WorkerW behind desktop icons
    │   wallpaper_host.*  │  Manages the render target window
    └──────────┬──────────┘
               │
    ┌──────────▼──────────┐    ┌─────────────────────┐
    │     Renderer        │◄───│   VideoDecoder       │
    │     renderer.*      │    │   video_decoder.*    │
    │  D3D11 device,      │    │  MF SourceReader,    │
    │  swap chain,        │    │  DXVA2 HW decode,    │
    │  texture blit       │    │  IMFSample → texture │
    └──────────┬──────────┘    └─────────────────────┘
               │
    ┌──────────▼──────────┐    ┌─────────────────────┐
    │    Timer            │    │   PowerMonitor       │
    │    timer.*          │    │   power_monitor.*    │
    │  High-res frame     │    │  Battery, idle,      │
    │  pacing (QPC)       │    │  fullscreen detect   │
    └─────────────────────┘    └─────────────────────┘
               │
    ┌──────────▼──────────┐
    │      Config         │
    │      config.*       │
    │  TOML parser,       │
    │  load/save settings │
    └─────────────────────┘
```

### Key Design Decisions

- **No exceptions, no RTTI** — smaller binary, deterministic control flow, HRESULT propagation throughout
- **ComPtr everywhere** — prevent COM leaks, automatic Release on scope exit
- **Integrated GPU preferred** — live wallpaper is background eye candy, not a benchmark; save the discrete GPU for games
- **Frame pacing via QPC** — `QueryPerformanceCounter` gives sub-microsecond precision for smooth frame delivery without busy-waiting
- **WorkerW injection** — uses the documented `SendMessage(Progman, 0x052C, ...)` technique to spawn a WorkerW behind desktop icons

## Logging

Logs are written to `%APPDATA%\LiveWallpaper\log.txt` with automatic rotation at 1 MB. Log levels:

| Level     | When                                      |
|-----------|-------------------------------------------|
| `ERROR`   | Failures that prevent operation            |
| `WARN`    | Recoverable issues                         |
| `INFO`    | State changes (start, pause, config load)  |
| `DEBUG`   | Verbose diagnostics (debug builds only)    |

## License

MIT License

Copyright (c) 2024 LiveWallpaper Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
# Live Wallpaper Engine — Full Audit & Engineering Validation Plan

Source Context: fileciteturn0file0

---

# 1. Audit Objective

This audit is NOT a normal code review.

The purpose is to:

* Validate whether the proposed architecture can actually hit the performance targets
* Detect hidden Windows-specific failure points
* Identify memory leaks, GPU stalls, threading risks, and power inefficiencies
* Ensure long-term stability for a continuously running desktop process
* Prevent future architectural dead-ends
* Define measurable engineering standards before implementation begins
* Produce a recovery/rebuild blueprint if the engine must later be rewritten

The audit assumes:

* Windows-only application
* Native C++ implementation
* Zero Electron
* Zero Chromium
* Zero web rendering stack
* Ultra-low idle resource usage
* Continuous background execution
* Hardware accelerated rendering pipeline

---

# 2. Locked Product Constraints

## Core Targets

| Metric        | Target                        |
| ------------- | ----------------------------- |
| Resolution    | 1920×1080                     |
| Codec         | H.264 MP4 only                |
| Audio         | Disabled entirely             |
| RAM           | ≤ 70 MB renderer target       |
| CPU           | ≤ 2% steady state             |
| GPU           | ≤ 3% preferred                |
| Startup       | Extremely aggressive          |
| Battery Mode  | Prefer Intel UHD              |
| Idle Behavior | Freeze frame after inactivity |
| Looping       | Hard restart                  |
| Platform      | Windows only                  |

These constraints are NON-NEGOTIABLE.

Every subsystem must be audited against them.

---

# 3. Architecture Areas That Must Be Audited

The audit must deeply inspect these engineering domains:

1. Desktop embedding architecture
2. Media decode pipeline
3. GPU presentation pipeline
4. Threading model
5. COM lifecycle handling
6. Windows Explorer recovery
7. Memory ownership
8. Device loss recovery
9. Power-state handling
10. Fullscreen detection
11. Idle detection
12. GPU selection behavior
13. Multi-monitor scaling
14. Crash resilience
15. Resource cleanup
16. Timing accuracy
17. Frame pacing
18. DWM interaction
19. Build system cleanliness
20. Binary size optimization
21. Startup latency
22. Long-session stability
23. Sleep/resume recovery
24. Logging architecture
25. Config safety

---

# 4. Core Engineering Audit Phases

---

## PHASE 1 — Architecture Validation

Purpose:
Verify the proposed system design is actually viable.

### Audit Tasks

#### 1. Validate WorkerW Injection

Audit:

* WorkerW creation reliability
* Explorer hierarchy assumptions
* Re-parenting behavior
* Desktop icon layering
* Win+D behavior
* Multi-monitor behavior
* Explorer restart recovery
* DPI scaling issues
* Windows 10 vs 11 differences

Failure Conditions:

* Wallpaper disappears after explorer restart
* Wallpaper overlays icons
* Wallpaper flickers on monitor changes
* Wallpaper breaks after Win+D

Deliverables:

* Injection lifecycle diagram
* Recovery strategy
* Fallback architecture
* Explorer watchdog design

---

#### 2. Validate Media Foundation Pipeline

Audit:

* Hardware decode path correctness
* DXVA2 availability
* Intel UHD decoder behavior
* RTX decoder fallback
* Surface transfer overhead
* COM object lifecycle
* Hardware/software decode fallback
* MP4 parsing reliability

Critical Questions:

* Are frames staying GPU-side?
* Are hidden CPU copies occurring?
* Does MF internally buffer too aggressively?
* Is frame queue growth bounded?

Failure Conditions:

* Software decoding unexpectedly activates
* CPU spikes during decode
* GPU memory leaks
* COM reference leaks

Deliverables:

* Decode pipeline map
* Memory ownership map
* Hardware fallback matrix

---

#### 3. Validate D3D11 Presentation Path

Audit:

* Texture upload path
* GPU synchronization stalls
* Swap chain configuration
* Composition overhead
* VSync interaction
* Frame pacing consistency
* Resource creation strategy

Critical Areas:

* Avoid CPU-side bitmap copies
* Avoid unnecessary texture recreation
* Avoid Present() stalls
* Avoid VRAM fragmentation

Failure Conditions:

* Frame hitching
* Present blocking
* DWM latency spikes
* GPU spikes above target

Deliverables:

* GPU frame lifecycle diagram
* Presentation timing model
* GPU synchronization strategy

---

## PHASE 2 — Performance Audit

Purpose:
Ensure the engine can actually meet performance targets.

---

### 1. Memory Audit

Audit Categories:

* Base process memory
* Media Foundation allocations
* D3D11 allocations
* Texture pools
* Frame queues
* UI memory
* Thread stacks
* Config cache
* Logging buffers
* COM allocations

Must Measure:

* Cold start RAM
* Idle RAM
* Playback RAM
* Long-session RAM growth
* VRAM usage
* Commit size
* Working set

Leak Detection:

* Repeated play/pause cycles
* Explorer restart loops
* Device loss recovery loops
* Sleep/resume cycles
* Wallpaper switching

Failure Conditions:

* Any unbounded memory growth
* VRAM growth over time
* Fragmentation spikes

Target:

* Stable long-term memory footprint

---

### 2. CPU Audit

Audit:

* Idle loop behavior
* Polling frequency
* Timer precision cost
* Fullscreen detection cost
* Idle detection overhead
* COM overhead
* Thread wake frequency
* Decode scheduling

Critical Goal:

The process must spend most time sleeping.

Failure Conditions:

* Busy-wait loops
* Excessive timer wakeups
* Background CPU spikes
* CPU spikes during monitor changes

Target:

* ≤ 2% sustained usage

---

### 3. GPU Audit

Audit:

* Decode utilization
* Presentation utilization
* Texture copy count
* Composition overhead
* Multi-GPU behavior
* Integrated GPU preference

Must Validate:

* Intel UHD is selected on battery
* RTX usage remains minimal
* No duplicate rendering paths

Failure Conditions:

* GPU spikes during idle
* Duplicate compositing
* High VRAM allocation churn

---

## PHASE 3 — Stability Audit

Purpose:
Guarantee continuous runtime stability.

---

### 1. Long Runtime Testing

Required Test Windows:

* 1 hour
* 6 hours
* 24 hours
* 72 hours

Audit:

* Memory drift
* Handle leaks
* COM leaks
* GPU leaks
* FPS degradation
* Timer drift
* Thread accumulation

---

### 2. Sleep / Resume Recovery

Audit:

* Suspend handling
* GPU device invalidation
* Media Foundation restart behavior
* Monitor reinitialization
* Timing recovery

Failure Conditions:

* Black wallpaper
* Frozen playback
* GPU device removed errors
* Resource duplication

---

### 3. Explorer Crash Recovery

Simulate:

* explorer.exe kill
* explorer.exe restart
* rapid restart loops

Must Validate:

* Automatic reinjection
* Resource cleanup
* No orphan windows
* No duplicate WorkerW bindings

---

### 4. Fullscreen Application Handling

Audit:

* Game detection accuracy
* Exclusive fullscreen behavior
* Borderless fullscreen behavior
* Rendering pause logic

Failure Conditions:

* Rendering continues behind games
* GPU remains active unnecessarily
* Wallpaper steals focus

---

## PHASE 4 — Threading & Concurrency Audit

Purpose:
Prevent race conditions and hidden synchronization stalls.

---

### Thread Model Validation

Current proposed model:

1. Main/UI thread
2. Render/decode thread

Audit:

* Cross-thread ownership
* COM apartment model correctness
* Shutdown synchronization
* Device context thread safety
* Atomic state management
* Lock contention

Failure Conditions:

* Deadlocks
* Shutdown hangs
* Render thread starvation
* Unsafe COM access

Deliverables:

* Thread ownership map
* Lock hierarchy
* Shutdown state machine

---

## PHASE 5 — Windows API Audit

Purpose:
Validate every Win32 interaction.

---

### APIs To Audit

#### Desktop Injection

* FindWindow
* SendMessageTimeout
* EnumWindows
* SetParent
* SetWindowPos

#### Power Management

* WM_POWERBROADCAST
* GetSystemPowerStatus

#### Idle Detection

* GetLastInputInfo

#### Fullscreen Detection

* GetForegroundWindow
* GetWindowRect
* MonitorFromWindow

#### Rendering

* D3D11CreateDevice
* IDXGISwapChain
* Present

#### Media Foundation

* IMFSourceReader
* IMFMediaType
* IMFTransform

Critical Requirement:

Every API failure path must be explicitly handled.

---

## PHASE 6 — Binary & Build Audit

Purpose:
Prevent future dependency bloat.

---

### Build System Audit

Validate:

* Clean CMake structure
* No hidden runtime dependencies
* Static runtime correctness
* Compiler optimization flags
* PDB generation
* Release stripping

---

### Binary Audit

Validate:

* Final executable size
* DLL dependencies
* Startup cost
* Runtime redistributables

Reject:

* Chromium
* Electron
* Qt
* .NET runtime
* Browser rendering engines

---

## PHASE 7 — Failure Scenario Audit

Purpose:
Map all catastrophic edge cases.

---

### Required Failure Simulations

1. GPU driver reset
2. Monitor unplug/replug
3. Resolution changes
4. DWM restart
5. Explorer restart
6. Battery mode switching
7. Sleep/resume loops
8. Corrupted MP4
9. Missing wallpaper file
10. Device removed errors
11. Out-of-memory scenarios
12. Multi-monitor topology changes

Each failure must define:

* Detection method
* Recovery path
* Cleanup path
* User-visible behavior
* Logging behavior

---

# 5. Required Audit Deliverables

The audit output must generate:

## 1. Architecture Blueprint

A complete reconstruction-level explanation of:

* Window hierarchy
* Decode pipeline
* Render pipeline
* Memory ownership
* Thread ownership
* Power management
* Startup flow
* Shutdown flow

---

## 2. Risk Matrix

Each subsystem must include:

| Component | Risk | Severity | Likelihood | Mitigation |
| --------- | ---- | -------- | ---------- | ---------- |

---

## 3. Performance Budget Sheet

Every subsystem receives a strict budget.

Example:

| System   | RAM  | CPU   | GPU |
| -------- | ---- | ----- | --- |
| Decoder  | 25MB | 0.5%  | 1%  |
| Renderer | 15MB | 0.3%  | 1%  |
| Watchdog | 2MB  | 0.05% | 0%  |

---

## 4. Recovery Blueprint

Must define:

* How to rebuild the project from scratch
* Required APIs
* Required threading model
* Required render path
* Required memory rules
* Required optimization rules

---

# 6. Engineering Rules (Mandatory)

These rules must be enforced during implementation.

## Never Add

* Embedded browsers
* Web rendering engines
* Node.js
* JavaScript runtime
* Telemetry
* Analytics
* Auto-update systems
* Cloud sync
* Background web services
* Plugin systems
* Excessive animations
* Dynamic scripting

---

## Always Prefer

* Native Win32
* Stack allocation where possible
* RAII ownership
* GPU-side frame handling
* Static memory patterns
* Event-driven systems
* Sleeping threads
* Explicit cleanup

---

# 7. Final Audit Success Criteria

The architecture passes only if:

* RAM remains stable for 72h
* No measurable leaks exist
* CPU stays ≤ 2%
* GPU stays ≤ 3%
* Explorer recovery works reliably
* Fullscreen pause works reliably
* Battery switching works correctly
* Sleep/resume never breaks rendering
* Startup is visually instant
* No external runtime dependencies exist
* Wallpaper never overlays icons incorrectly
* No frame hitching is visible

---

# 8. Recommended Audit Execution Order

1. WorkerW validation
2. Media Foundation prototype
3. D3D11 presentation prototype
4. Performance instrumentation
5. Threading validation
6. Explorer recovery testing
7. Sleep/resume testing
8. Long-session leak testing
9. Fullscreen detection validation
10. Final architecture lock

---

# 9. Expected Final Outcome

If the audit succeeds, the project should become:

* A highly optimized native Windows wallpaper engine
* Extremely lightweight
* Long-runtime stable
* Battery aware
* GPU efficient
* Recoverable after crashes
* Rebuildable from documentation alone
* Resistant to Windows desktop edge cases

The final system should behave closer to a native Windows subsystem than a traditional desktop app.

---

Source discussion and architectural decisions originated from the uploaded planning notes. fileciteturn0file0
