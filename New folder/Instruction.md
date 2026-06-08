# LiveWallpaper — Ultra-Optimized Windows Live Wallpaper Manager

## Complete Architecture & Implementation Instructions

---

## 1. Project Vision

### Purpose
A **single-purpose system utility** that plays video files as live desktop wallpapers on Windows with extreme efficiency. This is NOT a wallpaper engine clone, NOT a customization platform, NOT a marketplace. It is a lightweight video renderer that sits behind desktop icons and consumes near-zero resources.

### Philosophy
- **Invisible when idle** — no CPU/GPU usage when desktop is not visible
- **Responsive when active** — sub-second startup, instant wallpaper display
- **Stable under stress** — survives explorer.exe crashes, monitor reconnects, sleep/wake
- **Efficient on laptops** — battery life is the #1 constraint
- **Native to Windows** — uses Win32/COM APIs, no frameworks, no Electron, no Chromium
- **Cleanly engineered** — modular, no technical debt, no feature bloat

### Target Hardware
- **CPU**: Intel i5-12450HX (8C/12T, hybrid P+E cores)
- **RAM**: 16GB DDR5
- **GPU (Discrete)**: NVIDIA RTX 3050 Laptop (NVDEC H.264 support)
- **GPU (Integrated)**: Intel UHD Graphics (Quick Sync H.264 support)
- **OS**: Windows 10/11

### Target User
- Single user (developer/power user)
- Offline-only operation
- Local wallpaper files only
- No community, no sharing, no cloud

---

## 2. Wallpaper Specification

### Supported Format
| Property | Value |
|---|---|
| **Container** | MP4 |
| **Video Codec** | H.264 (AVC) only |
| **Resolution** | 1920×1080 (native) |
| **Audio** | None — stripped/ignored entirely |
| **Frame Rate** | User-configurable (default: 24 FPS) |

### Why H.264 Only
- Most mature hardware decode support on both Intel Quick Sync and NVIDIA NVDEC
- Lower decode complexity than H.265 (~30-40% less processing)
- Lower RAM usage than H.265 (smaller decode buffers)
- Universal hardware acceleration via DXVA2
- One codec path = simpler code, fewer bugs, smaller binary

### Forbidden Formats
- **GIF** — uncompressed frames, massive RAM usage, CPU-decoded
- **WebM/VP9** — spotty hardware decode on Intel UHD
- **AV1** — RTX 3050 lacks AV1 hardware decode
- **H.265/HEVC** — higher decode complexity, unnecessary for this use case
- **WebGL/HTML** — requires Chromium/webview (bloat)
- **Shaders/Scenes** — out of scope, adds massive complexity

### Looping Behavior
- **Hard restart** — when video reaches end, seek to frame 0 and restart
- No crossfade, no blend transitions
- Acceptable: brief flicker/gap at loop point (simplest implementation)
- Future consideration: gapless loop via pre-buffering (not v1)

### Idle Behavior
- After **N minutes** of no mouse/keyboard input, **freeze on current frame**
- Detection: `GetLastInputInfo()` Win32 API — single integer comparison, near-zero cost
- Resume playback instantly when user input detected
- Configurable idle timeout (default: 5 minutes)

---

## 3. Performance Targets

### Hard Limits

| Metric | Target | Hard Maximum |
|---|---|---|
| **RAM (renderer)** | < 50MB | 70MB |
| **CPU (steady-state)** | < 1% | 2% |
| **GPU utilization** | < 1% | 3% |
| **Startup burst CPU** | Up to 30% | 30% (acceptable for fast startup) |
| **Startup time** | < 1 second | 2 seconds |

### Power-Aware Behavior
- If CPU is power-constrained (thermal throttle, power limit), **stop rendering**
- On battery: **immediately freeze wallpaper** (static screenshot of last frame)
- Architecture leaves room for future per-wallpaper battery policies (Option D)

### Resource Priority
1. Battery life (highest priority)
2. CPU usage
3. RAM usage
4. GPU usage
5. Visual quality (lowest priority)

---

## 4. Rendering Architecture

### 4.1 Desktop Injection — WorkerW Window Hijack

The wallpaper is displayed by embedding a render surface into the hidden `WorkerW` window that sits behind desktop icons.

#### How It Works
```
Step 1: Find the "Progman" window (Program Manager)
        → FindWindow("Progman", NULL)

Step 2: Send undocumented message 0x052C to Progman
        → SendMessageTimeout(progman, 0x052C, 0, 0, ...)
        This forces Windows to spawn a hidden WorkerW window between
        the desktop icons and the wallpaper background.

Step 3: Enumerate windows to find the WorkerW with SHELLDLL_DefView child
        → EnumWindows() callback
        → FindWindowEx(hwnd, NULL, "SHELLDLL_DefView", NULL)
        → The NEXT WorkerW sibling is our target

Step 4: Embed our render window as a child of this WorkerW
        → SetParent(ourWindow, workerW)
        → Resize to fill the entire monitor
```

#### Recovery Strategy
- Monitor `explorer.exe` process using `RegisterWaitForSingleObject` on the process handle
- When `explorer.exe` restarts, wait 2 seconds for shell to stabilize, then re-run injection
- Also handle `WM_DISPLAYCHANGE` for monitor resolution changes

#### Window Setup
```
- Style: WS_CHILD | WS_VISIBLE (no border, no title, no frame)
- Size: Full monitor resolution (1920×1080)
- Position: 0, 0 relative to WorkerW
- Class: Custom registered class with CS_OWNDC
```

### 4.2 Video Decode Pipeline — Windows Media Foundation

**Why Media Foundation over FFmpeg:**
- Zero external dependencies (built into Windows)
- Automatic DXVA2 hardware acceleration
- Native D3D11 texture output
- Smallest possible binary size
- Perfect for "one codec, one resolution" scope

#### Pipeline Architecture
```
MP4 File → IMFSourceReader → DXVA2 Hardware Decoder → ID3D11Texture2D (GPU memory)
                                    ↓
                            Intel Quick Sync (on battery)
                            or NVDEC (when plugged in, if preferred)
```

#### Setup Steps
```
1. Create D3D11 Device
   → D3D11CreateDevice() with D3D_DRIVER_TYPE_HARDWARE
   → Use Intel UHD adapter on battery, auto-select when plugged in

2. Create DXGI Device Manager
   → MFCreateDXGIDeviceManager()
   → ResetDevice() with our D3D11 device

3. Create Source Reader
   → MFCreateSourceReaderFromURL(videoPath, attributes)
   → Set MF_SOURCE_READER_D3D_MANAGER attribute to DXGI manager
   → Set MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS = TRUE

4. Configure Output Type
   → SetCurrentMediaType() to MFVideoFormat_NV12 or MFVideoFormat_ARGB32
   → Decoded frames arrive as ID3D11Texture2D

5. Strip Audio
   → reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE)
   → reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE)
```

#### Frame Decode Loop
```
1. WaitableTimer fires every ~41.67ms (for 24 FPS)
2. Call reader->ReadSample() — returns IMFSample
3. Get IMFMediaBuffer → IMFDXGIBuffer → ID3D11Texture2D
4. Copy/blit texture to swap chain back buffer
5. Present()
```

#### Video Looping
```
When ReadSample() returns MF_E_END_OF_STREAM:
  → Create PROPVARIANT with position = 0
  → reader->SetCurrentPosition(GUID_NULL, &startPosition)
  → Continue decode loop
```

### 4.3 Frame Presentation — D3D11 Blit

#### Why D3D11
- Decoded frame stays in GPU memory the entire time — zero CPU copies
- Simple shader-free blit (CopyResource or CopySubresourceRegion)
- Well-supported on both Intel UHD and RTX 3050
- Minimal code surface

#### Swap Chain Setup
```
1. Create DXGI Swap Chain for our wallpaper HWND
   → DXGI_SWAP_CHAIN_DESC1:
     - Width: 1920, Height: 1080
     - Format: DXGI_FORMAT_B8G8R8A8_UNORM
     - BufferCount: 2 (double buffering — minimal VRAM)
     - SwapEffect: DXGI_SWAP_EFFECT_FLIP_DISCARD (modern, efficient)
     - Flags: 0 (no special flags)

2. On each frame:
   → Get decoded texture from Media Foundation
   → CopyResource() from decoded texture to back buffer
   → Present(0, 0) — no vsync wait (DWM handles composition)
```

#### Why No VSync
- Desktop Window Manager (DWM) already composites at the monitor's refresh rate
- Our swap chain just needs to have the latest frame ready
- Present(0, 0) = immediate, non-blocking
- No need for our app to sync to 60Hz when we only produce 24 FPS

### 4.4 Frame Timing — Waitable Timer

```
- CreateWaitableTimerEx() with HIGH_RESOLUTION flag
- SetWaitableTimer() with period = 1000/FPS milliseconds
- WaitForSingleObject() in the render thread
- One frame decoded and presented per timer tick
```

#### Why Waitable Timer Over Other Methods
- More precise than SetTimer (SetTimer has ~15ms granularity)
- Lower overhead than busy-wait loop
- Thread sleeps between frames — CPU usage drops to near-zero
- HIGH_RESOLUTION flag available on Windows 10+ for sub-ms precision

---

## 5. Process & Threading Model

### Single Process Architecture
```
┌─────────────────────────────────────────────┐
│              LiveWallpaper.exe               │
│                                             │
│  ┌─────────────┐     ┌──────────────────┐   │
│  │ Main Thread  │     │  Render Thread   │   │
│  │             │     │                  │   │
│  │ - Win32 Msg │     │ - Waitable Timer │   │
│  │   Pump      │     │ - MF ReadSample  │   │
│  │ - Tray Icon │     │ - D3D11 Present  │   │
│  │ - Settings  │     │ - Idle Detection │   │
│  │ - Power     │     │ - Loop Logic     │   │
│  │   Events    │     │                  │   │
│  └──────┬──────┘     └──────────────────┘   │
│         │                                   │
│         │ Signals (Events)                  │
│         │ - Pause/Resume                    │
│         │ - Change Video                    │
│         │ - Shutdown                        │
│         │ - Power State Change              │
│         └───────────────────────────────────│
└─────────────────────────────────────────────┘
```

### Main Thread Responsibilities
- Win32 message pump (`GetMessage`/`DispatchMessage`)
- System tray icon and context menu
- Power state monitoring (`WM_POWERBROADCAST`)
- Display change monitoring (`WM_DISPLAYCHANGE`)
- Explorer.exe crash detection and recovery
- Fullscreen app detection (periodic check on timer)
- User idle detection (`GetLastInputInfo`)
- Send control signals to render thread via Events

### Render Thread Responsibilities
- Video decode via Media Foundation (hardware-accelerated)
- Frame presentation via D3D11
- Frame timing via Waitable Timer
- Respond to control signals (pause, resume, change video, shutdown)

### Inter-Thread Communication
```
Main → Render: Windows Event objects (CreateEvent)
  - hPauseEvent   → render thread pauses decode loop
  - hResumeEvent  → render thread resumes decode loop
  - hChangeEvent  → render thread loads new video file
  - hShutdownEvent → render thread exits cleanly

Render → Main: PostMessage() for status updates (optional, v2)
```

### Why Single Process (Not Separate Renderer Process)
- Video wallpaper for one monitor = one decode pipeline
- IPC adds latency, complexity, and memory overhead
- Process boundary prevents sharing D3D11 device/textures directly
- Separate process is only justified for sandboxing (not needed here)
- Single process with two threads is the simplest correct architecture

---

## 6. Windows Integration

### 6.1 Fullscreen App / Game Detection

When a fullscreen application covers the desktop, the wallpaper is completely hidden. Rendering is pure waste.

#### Detection Method
```cpp
// Check every 2 seconds via a timer in main thread
QUERY_USER_NOTIFICATION_STATE state;
SHQueryUserNotificationState(&state);

if (state == QUNS_RUNNING_D3D_FULL_SCREEN ||
    state == QUNS_BUSY ||
    state == QUNS_PRESENTATION_MODE) {
    // Signal render thread to pause
    SetEvent(hPauseEvent);
} else {
    // Signal render thread to resume
    SetEvent(hResumeEvent);
}
```

#### Why This Method
- Single API call, no window enumeration
- Handles games, fullscreen video players, presentations
- Near-zero overhead (called every 2 seconds)
- Built into Windows Shell API

### 6.2 Explorer.exe Crash Recovery

```
1. Get explorer.exe PID → CreateToolhelp32Snapshot + Process32First/Next
2. Open handle → OpenProcess(SYNCHRONIZE, FALSE, explorerPid)
3. Register callback → RegisterWaitForSingleObject(handle, callback, ...)
4. On callback fire:
   a. Wait 2000ms for new explorer.exe to stabilize
   b. Re-run WorkerW injection sequence
   c. Re-parent render window to new WorkerW
   d. Re-register for next crash
```

### 6.3 Power State Management

```cpp
// In WndProc:
case WM_POWERBROADCAST:
    if (wParam == PBT_APMPOWERSTATUSCHANGE) {
        SYSTEM_POWER_STATUS sps;
        GetSystemPowerStatus(&sps);

        if (sps.ACLineStatus == 0) {
            // ON BATTERY → Freeze wallpaper
            SetEvent(hPauseEvent);
        } else {
            // ON AC POWER → Resume wallpaper
            SetEvent(hResumeEvent);
        }
    }
    break;
```

### 6.4 User Idle Detection

```cpp
// Check every 30 seconds via timer in main thread
LASTINPUTINFO lii = { sizeof(LASTINPUTINFO) };
GetLastInputInfo(&lii);

DWORD idleMs = GetTickCount() - lii.dwTime;
DWORD idleThresholdMs = idleTimeoutMinutes * 60 * 1000;

if (idleMs > idleThresholdMs) {
    SetEvent(hPauseEvent);  // Freeze on current frame
} else {
    SetEvent(hResumeEvent); // Resume if was frozen
}
```

### 6.5 Startup Strategy

- **Method**: Windows Task Scheduler (NOT registry Run key)
- **Why**: Task Scheduler supports delayed start, conditions (AC power only), and is the modern recommended approach
- **Startup behavior**: Launch minimized to tray, inject WorkerW, start rendering
- **CPU burst**: Up to 30% CPU acceptable during startup for fast wallpaper display

### 6.6 GPU Selection

```
On Battery:
  → Enumerate DXGI adapters
  → Select Intel UHD (integrated) — lowest power draw
  → Intel Quick Sync handles H.264 decode with near-zero power impact

On AC Power:
  → Use system default adapter (typically Intel UHD for efficiency)
  → RTX 3050 should remain in low-power sleep state
  → A 1080p H.264 decode at 24 FPS is trivial for integrated graphics

Override:
  → User can force a specific GPU in settings (future, not v1)
```

**Critical insight**: Using the RTX 3050 for wallpaper decode would wake the discrete GPU from sleep, drawing 5-15W of additional power for a task that the Intel UHD handles at <1W. Always prefer integrated GPU for this workload.

---

## 7. Memory Lifecycle

### Memory Budget Breakdown (Target: < 50MB, Max: 70MB)

| Component | Estimated RAM |
|---|---|
| Process overhead (PE, stack, heap) | ~5MB |
| D3D11 device + swap chain (2× 1080p buffers) | ~16MB |
| Media Foundation pipeline | ~10MB |
| DXVA2 decode buffers (3-4 reference frames) | ~12MB |
| Working set (code, data) | ~5MB |
| **Total** | **~48MB** |

### Memory Strategy
- **No dynamic allocation in render loop** — all buffers allocated at startup
- **Fixed buffer count** — 2 swap chain buffers, MF manages decode buffers internally
- **No frame queue** — decode one frame, present immediately, decode next
- **Release resources on pause** — when wallpaper is frozen, release MF source reader (keep D3D11 device)
- **Full cleanup on idle** — after extended idle, release everything except the frozen frame texture

### Leak Prevention
- COM pointers wrapped in `ComPtr<T>` (WRL) — automatic Release()
- D3D11 resources tracked and explicitly released
- MF pipeline shut down with `MFShutdown()` on exit
- No raw `new`/`malloc` in steady-state code

---

## 8. Configuration System

### Config Format: TOML
```toml
[wallpaper]
path = "D:\\Wallpapers\\cyberpunk_city.mp4"
fps = 24

[behavior]
idle_timeout_minutes = 5
pause_on_battery = true
pause_on_fullscreen = true

[advanced]
gpu_preference = "integrated"   # "integrated" | "discrete" | "auto"
```

### Why TOML
- Human-readable and editable
- Simpler than JSON (no quotes on keys, supports comments)
- Lighter to parse than YAML
- Single-file config — `%APPDATA%\LiveWallpaper\config.toml`

### Config Parsing
- Use a minimal TOML parser (single .h/.c file, e.g., toml-c or hand-rolled for 10 keys)
- Config loaded once at startup, cached in memory
- Config changes trigger events to render thread (pause, change FPS, change video)

---

## 9. UI — Minimal Tray Application

### System Tray Only
- **No main window** — the app is the tray icon
- **Right-click context menu**:
  ```
  ──────────────────────
  ▶ Resume / ⏸ Pause
  ──────────────────────
  📁 Change Wallpaper...
  ⚙ Settings...
  ──────────────────────
  ❌ Exit
  ──────────────────────
  ```

### Settings Dialog
- **Native Win32 dialog** (DialogBox + resource template)
- Simple controls: file picker, FPS slider, checkboxes for behavior toggles
- **No custom UI framework** — Win32 dialogs are 0 dependencies, instant load
- Settings apply immediately (signal render thread via events)

### Tray Icon Behavior
- Single left-click: toggle pause/resume
- Right-click: context menu
- Tooltip: "LiveWallpaper — Playing [filename] at [FPS] FPS" or "Paused"
- Icon changes between play/pause states

---

## 10. Language Decision: C++ (with C-style core)

### Why C++ Over Rust
- **COM/Win32/D3D11**: All APIs are natively C/C++. Rust bindings (windows-rs) add abstraction and compile time
- **Media Foundation**: Heavily COM-based, natural in C++ with ComPtr<T>
- **D3D11**: C++ is the first-class citizen, all examples and docs are C++
- **Compile speed**: Faster iteration for a small project
- **Binary size**: C++ with static CRT produces smallest binaries
- **Your hardware**: No cross-platform needed, Windows-native is ideal

### Where Rust Could Be Used (Future)
- Config parser module (if extracted as a library)
- Plugin system (if ever added)
- CLI tool for wallpaper management

### C++ Standards & Guidelines
- **Standard**: C++17 (for `std::filesystem`, `std::optional`, structured bindings)
- **No STL containers in hot path** — raw arrays/fixed buffers for render loop
- **No exceptions** — return codes, HRESULT checking
- **No RTTI** — disabled in compiler flags
- **ComPtr<T>** from WRL for all COM pointers
- **Compiler**: MSVC (Visual Studio 2022 Build Tools)
- **Optimization**: `/O2 /GL /LTCG` for release builds

---

## 11. Project Structure

```
LiveWallpaper/
├── src/
│   ├── main.cpp              # Entry point, message pump, tray icon
│   ├── wallpaper_host.h/cpp  # WorkerW injection, window management
│   ├── video_decoder.h/cpp   # Media Foundation decode pipeline
│   ├── renderer.h/cpp        # D3D11 device, swap chain, frame presentation
│   ├── power_monitor.h/cpp   # Battery, idle, fullscreen detection
│   ├── config.h/cpp          # TOML config loading
│   ├── timer.h/cpp           # High-resolution waitable timer
│   └── utils.h               # Logging, error macros, HRESULT helpers
├── res/
│   ├── tray_icon.ico         # System tray icon
│   ├── settings_dialog.rc    # Win32 dialog resource
│   └── resource.h            # Resource IDs
├── config/
│   └── default_config.toml   # Default configuration
├── build/
│   └── CMakeLists.txt        # CMake build (or a simple build.bat)
├── Instruction.md            # This file
└── README.md                 # Usage documentation
```

### Module Responsibilities

| Module | Responsibility | Dependencies |
|---|---|---|
| `main.cpp` | Message pump, tray icon, orchestration | All modules |
| `wallpaper_host` | Find WorkerW, create child window, handle recovery | Win32 |
| `video_decoder` | MF source reader, DXVA2 decode, frame extraction | MF, D3D11 |
| `renderer` | D3D11 device, swap chain, texture blit, present | D3D11, DXGI |
| `power_monitor` | Battery state, idle detection, fullscreen detection | Win32 Shell |
| `config` | TOML parsing, settings management | None |
| `timer` | Waitable timer creation, FPS-based period | Win32 |

### Module Coupling Rules
- **No module includes another module's .cpp** — header-only interfaces
- **Render thread only touches**: `video_decoder`, `renderer`, `timer`
- **Main thread only touches**: `wallpaper_host`, `power_monitor`, `config`, tray UI
- **Communication**: Windows Event objects (no shared state, no mutexes on hot path)

---

## 12. Build System

### CMake Configuration
```cmake
cmake_minimum_required(VERSION 3.20)
project(LiveWallpaper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(LiveWallpaper WIN32
    src/main.cpp
    src/wallpaper_host.cpp
    src/video_decoder.cpp
    src/renderer.cpp
    src/power_monitor.cpp
    src/config.cpp
    src/timer.cpp
    res/settings_dialog.rc
)

target_link_libraries(LiveWallpaper PRIVATE
    d3d11
    dxgi
    mf
    mfplat
    mfreadwrite
    mfuuid
    shlwapi
    shell32
    user32
    gdi32
    ole32
)

# Optimization flags for Release
target_compile_options(LiveWallpaper PRIVATE
    $<$<CONFIG:Release>:/O2 /GL /GS- /Oi /fp:fast>
)
target_link_options(LiveWallpaper PRIVATE
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
)
```

### Dependencies: ZERO External
- All APIs are Windows SDK built-ins
- No FFmpeg, no SDL, no external media libraries
- Config parser: hand-rolled minimal TOML parser (~200 lines)
- Total external dependency count: **0**

---

## 13. Startup Sequence (Detailed)

```
1. WinMain() entry
   ├── Parse command-line args (--config path, --wallpaper path)
   ├── Load config from TOML file
   ├── Initialize COM (CoInitializeEx with COINIT_MULTITHREADED)
   ├── Initialize Media Foundation (MFStartup)
   │
2. Create D3D11 Device
   ├── Enumerate DXGI adapters
   ├── Select integrated GPU (Intel UHD) if on battery
   ├── Create device with D3D11_CREATE_DEVICE_VIDEO_SUPPORT flag
   │
3. Inject into Desktop
   ├── Find Progman window
   ├── Send 0x052C message
   ├── Find target WorkerW
   ├── Create child window (WS_CHILD)
   ├── Create swap chain for child window
   │
4. Start Render Thread
   ├── Create MF Source Reader with DXVA2
   ├── Configure for video-only (no audio streams)
   ├── Create waitable timer (period = 1000/FPS ms)
   ├── Enter decode-present loop
   │
5. Main Thread Continues
   ├── Create system tray icon
   ├── Register for WM_POWERBROADCAST
   ├── Start idle detection timer (30-second interval)
   ├── Start fullscreen detection timer (2-second interval)
   ├── Register explorer.exe crash watcher
   ├── Enter message pump (GetMessage loop)
   │
6. Wallpaper visible in < 1 second from launch
```

---

## 14. Render Loop (Detailed)

```cpp
// Render thread entry point
void RenderThread(RenderContext* ctx) {
    // Pre-allocated, no dynamic allocation in this loop

    while (true) {
        // Wait for timer tick OR control event
        HANDLE handles[] = { ctx->hTimer, ctx->hPauseEvent,
                             ctx->hChangeEvent, ctx->hShutdownEvent };
        DWORD result = WaitForMultipleObjects(4, handles, FALSE, INFINITE);

        switch (result) {
        case WAIT_OBJECT_0: // Timer tick → decode & present
        {
            IMFSample* sample = nullptr;
            DWORD flags = 0;
            HRESULT hr = ctx->reader->ReadSample(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                0, nullptr, &flags, nullptr, &sample);

            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                // Loop: seek to beginning
                PROPVARIANT pos = {};
                pos.vt = VT_I8;
                pos.hVal.QuadPart = 0;
                ctx->reader->SetCurrentPosition(GUID_NULL, &pos);
                continue;
            }

            if (SUCCEEDED(hr) && sample) {
                // Get D3D11 texture from decoded frame
                IMFMediaBuffer* buffer = nullptr;
                sample->GetBufferByIndex(0, &buffer);

                IMFDXGIBuffer* dxgiBuffer = nullptr;
                buffer->QueryInterface(&dxgiBuffer);

                ID3D11Texture2D* texture = nullptr;
                UINT subresource = 0;
                dxgiBuffer->GetResource(IID_PPV_ARGS(&texture));
                dxgiBuffer->GetSubresourceIndex(&subresource);

                // Blit to swap chain
                ID3D11Texture2D* backBuffer = nullptr;
                ctx->swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
                ctx->deviceContext->CopySubresourceRegion(
                    backBuffer, 0, 0, 0, 0,
                    texture, subresource, nullptr);

                ctx->swapChain->Present(0, 0);

                // Release (ComPtr handles this automatically in real code)
                backBuffer->Release();
                texture->Release();
                dxgiBuffer->Release();
                buffer->Release();
                sample->Release();
            }
            break;
        }

        case WAIT_OBJECT_0 + 1: // Pause → enter frozen state
            WaitForSingleObject(ctx->hResumeEvent, INFINITE);
            break;

        case WAIT_OBJECT_0 + 2: // Change video → reload
            ReloadVideo(ctx);
            break;

        case WAIT_OBJECT_0 + 3: // Shutdown → exit thread
            return;
        }
    }
}
```

### Critical Performance Notes
- `WaitForMultipleObjects` puts the thread to sleep — **zero CPU** between frames
- `ReadSample` with DXVA2 offloads decode to GPU hardware — **near-zero CPU**
- `CopySubresourceRegion` is a GPU-to-GPU copy — **zero CPU memory bandwidth**
- `Present(0, 0)` is non-blocking — **no vsync stall**
- **Total CPU work per frame**: ~0.1ms of API call overhead
- **At 24 FPS**: CPU active for ~2.4ms per second out of 1000ms = **0.24% CPU**

---

## 15. Battery Optimization Strategy

### Current Implementation (Option A)
```
AC Power → Full rendering at configured FPS
Battery  → Immediately freeze on current frame
            - Cancel waitable timer
            - MF source reader paused (no decode)
            - GPU idle (no draw calls)
            - Effective resource usage: ~0% CPU, ~0% GPU, ~5MB RAM (frozen texture)
```

### Future Architecture for Option D (Per-Wallpaper Policies)
```
Config structure (future):
[wallpaper.battery_policy]
mode = "freeze"          # "freeze" | "reduce_fps" | "reduce_quality" | "continue"
battery_fps = 12         # FPS when in reduce_fps mode
freeze_threshold = 20    # Battery % below which to freeze regardless
```

This requires:
- Battery percentage monitoring (already available via `GetSystemPowerStatus`)
- Per-wallpaper config sections
- FPS change without decoder restart (just change timer period)

The current architecture supports this cleanly — the render thread already responds to events, and changing the waitable timer period is a single API call.

---

## 16. Error Handling Strategy

### HRESULT Checking
```cpp
#define HR_CHECK(hr, msg) \
    do { \
        if (FAILED(hr)) { \
            LogError(msg, hr); \
            return hr; \
        } \
    } while(0)
```

### Crash Recovery
- **Unhandled exception filter**: `SetUnhandledExceptionFilter` — log crash, write minidump
- **Explorer crash**: Auto-recover via process watch (Section 6.2)
- **D3D11 device lost**: `DXGI_ERROR_DEVICE_REMOVED` — recreate device and swap chain
- **Video file error**: Fall back to last valid frame, log error

### Logging
- Minimal file logger to `%APPDATA%\LiveWallpaper\log.txt`
- Log rotation: single file, max 1MB, overwrite oldest
- Log levels: ERROR, WARN, INFO (DEBUG only in debug builds)
- No logging in render loop hot path (only on state changes)

---

## 17. Installation Strategy

### Portable Application
- **No installer** — single `.exe` + `config.toml`
- Copy to any folder, run
- First-run: creates `%APPDATA%\LiveWallpaper\` for config and logs
- Auto-start: creates Windows Task Scheduler task (optional, user-triggered)

### File Layout (Installed)
```
LiveWallpaper/
├── LiveWallpaper.exe    # ~200-500KB release binary
└── config.toml          # User configuration

%APPDATA%\LiveWallpaper/
├── config.toml          # Runtime config (copied on first run)
└── log.txt              # Log file
```

---

## 18. What Will NEVER Be Added

| Feature | Reason |
|---|---|
| Web/HTML wallpapers | Requires Chromium/WebView2 — 100MB+ memory |
| Unity/Unreal scenes | Massive runtime, defeats the purpose |
| Built-in wallpaper editor | Out of scope, use external tools |
| Online marketplace | No network code, ever |
| Social features | Single user app |
| Telemetry/analytics | Offline only, no tracking |
| DRM | No licensing, no protection |
| Audio playback | Unnecessary complexity for wallpapers |
| GIF support | Terrible efficiency |
| Shader wallpapers | Adds GPU pipeline complexity |
| Plugin system (v1) | Keep it simple, direct code only |
| Auto-updates | Offline, manual updates |
| Multi-language UI | Single user, English only |

---

## 19. Development Phases

### Phase 1: Core Engine (MVP)
- [ ] D3D11 device creation with GPU selection
- [ ] WorkerW window injection
- [ ] Media Foundation H.264 decode with DXVA2
- [ ] D3D11 texture blit to swap chain
- [ ] Waitable timer frame pacing (24 FPS)
- [ ] Hard-restart video looping
- [ ] Basic console output for debugging

### Phase 2: System Integration
- [ ] System tray icon with context menu
- [ ] Power state detection (AC/battery)
- [ ] Pause on battery
- [ ] Idle detection and freeze
- [ ] Fullscreen app detection and pause
- [ ] Explorer.exe crash recovery
- [ ] TOML config file loading

### Phase 3: User Features
- [ ] Settings dialog (Win32 native)
- [ ] File picker for wallpaper selection
- [ ] FPS configuration slider
- [ ] Idle timeout configuration
- [ ] Task Scheduler auto-start setup

### Phase 4: Polish & Hardening
- [ ] D3D11 device lost recovery
- [ ] Minidump crash handler
- [ ] Log rotation
- [ ] Memory profiling and optimization
- [ ] Startup time optimization
- [ ] Binary size optimization

---

## 20. Success Criteria

The app is DONE when:

| Criteria | Target |
|---|---|
| Wallpaper plays behind desktop icons | ✅ Working |
| H.264 1080p MP4 at 24 FPS | ✅ Smooth |
| RAM usage (renderer) | < 50MB |
| CPU usage (steady-state) | < 1% |
| GPU usage | < 1% |
| Battery: immediate freeze | ✅ Working |
| Idle: freeze after timeout | ✅ Working |
| Fullscreen: pause rendering | ✅ Working |
| Explorer crash: auto-recover | ✅ Working |
| Startup to visible wallpaper | < 1 second |
| Binary size | < 500KB |
| External dependencies | 0 |
| Tray icon with controls | ✅ Working |
| Stable for 24+ hours continuous | ✅ No leaks |

---

## Appendix A: Key Win32 / COM APIs Used

| API | Purpose |
|---|---|
| `FindWindow` / `FindWindowEx` | Locate Progman and WorkerW |
| `SendMessageTimeout` | Trigger WorkerW creation (0x052C) |
| `SetParent` | Embed render window in WorkerW |
| `D3D11CreateDevice` | Create GPU device |
| `CreateDXGIFactory1` | Enumerate GPU adapters |
| `IDXGISwapChain1::Present` | Display frames |
| `MFCreateSourceReaderFromURL` | Open video file |
| `IMFSourceReader::ReadSample` | Decode video frames |
| `MFCreateDXGIDeviceManager` | Link MF to D3D11 for hardware decode |
| `CreateWaitableTimerEx` | High-resolution frame timer |
| `WaitForMultipleObjects` | Thread synchronization |
| `Shell_NotifyIcon` | System tray icon |
| `GetSystemPowerStatus` | Battery state |
| `GetLastInputInfo` | User idle detection |
| `SHQueryUserNotificationState` | Fullscreen app detection |
| `RegisterWaitForSingleObject` | Explorer crash monitoring |
| `SetUnhandledExceptionFilter` | Crash dump |

## Appendix B: Compiler & Linker Flags

### Debug Build
```
/Od /Zi /RTC1 /MDd /D_DEBUG /DCONSOLE_LOGGING
```

### Release Build
```
/O2 /GL /GS- /Oi /fp:fast /MT /DNDEBUG
/LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:WINDOWS
```

### Key Flags Explained
- `/O2` — Maximum speed optimization
- `/GL` + `/LTCG` — Whole-program optimization (cross-module inlining)
- `/GS-` — Disable buffer security checks (we validate all inputs)
- `/Oi` — Enable intrinsic functions
- `/fp:fast` — Fast floating-point (not precision-critical)
- `/MT` — Static CRT link (no vcruntime DLL dependency)
- `/OPT:REF` — Remove unreferenced functions
- `/OPT:ICF` — Fold identical COMDATs

---

*This document is the single source of truth for the LiveWallpaper project architecture.*
*All implementation must follow these specifications.*
*Any deviation requires updating this document first.*
