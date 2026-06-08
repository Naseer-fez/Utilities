// ============================================================================
// main.cpp — Application entry point and orchestrator for LiveWallpaper
// ============================================================================

#include "utils.h"
#include "renderer.h"
#include "video_decoder.h"
#include "timer.h"
#include "wallpaper_host.h"
#include "power_monitor.h"
#include "config.h"
#include "telemetry.h"
#include "../res/resource.h"

#include <mfapi.h>
#include <commctrl.h>   // Trackbar (FPS slider), common controls
#include <commdlg.h>    // GetOpenFileNameW (file picker)
#include <shellapi.h>   // Shell_NotifyIconW, NOTIFYICONDATAW
#include <wtsapi32.h>   // WTSRegisterSessionNotification

// Enable visual styles for common controls v6
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Link against Wtsapi32 for session notifications
#pragma comment(lib, "wtsapi32.lib")

namespace {

// --- Configuration ---
lw::AppConfig     g_config;

// --- Core subsystems ---
lw::Renderer      g_renderer;
lw::VideoDecoder  g_decoder;
lw::WallpaperHost g_host;
lw::FrameTimer    g_timer;

// --- Inter-thread events ---
HANDLE g_hPauseEvent    = nullptr;   // Main → Render: pause decode loop
HANDLE g_hResumeEvent   = nullptr;   // Main → Render: resume decode loop
HANDLE g_hChangeEvent   = nullptr;   // Main → Render: reload video file
HANDLE g_hShutdownEvent = nullptr;   // Main → Render: exit thread
HANDLE g_hRecoverEvent  = nullptr;   // Main → Render: recreate swapchains after recovery

// --- Tray icon ---
NOTIFYICONDATAW g_nid = {};       // System tray notification icon data
bool g_isPaused = false;          // Current pause state (for tray icon toggle)

// --- Render thread ---
HANDLE g_renderThread = nullptr;     // Render thread handle

// --- Message-only window ---
HWND g_msgWnd = nullptr;             // Hidden HWND_MESSAGE window for power setting transitions, etc.

// --- Single-instance mutex ---
HANDLE g_instanceMutex = nullptr;

// --- Tracking pause reasons to avoid incorrect resume ---
bool g_pausedByBattery    = false;
bool g_pausedBySessionLock = false;
bool g_pausedByIdle       = false;
bool g_pausedByUser       = false; // Explicit user pause via tray

// Window class name for our message-only window
const wchar_t* const kMsgWndClass = L"LiveWallpaper_MsgWnd";

// Timer IDs for non-polling checks and shell recovery
constexpr UINT_PTR IDT_IDLE_CHECK       = 1002;
constexpr UINT_PTR IDT_RECOVER_SHELL    = 1003;

// WinEvent hook handles (instant event-driven fullscreen tracking)
HWINEVENTHOOK g_hEventHookForeground = nullptr;
HWINEVENTHOOK g_hEventHookMoveSize   = nullptr;
HWINEVENTHOOK g_hEventHookMinimize   = nullptr;

// Power notification registration handle
HPOWERNOTIFY g_hPowerNotify = nullptr;

// Thread-safe custom message definitions
constexpr UINT WM_EXPLORER_CRASHED      = WM_USER + 100;
constexpr UINT WM_USER_CHECK_FULLSCREEN = WM_USER + 101;

UINT g_uTaskbarCreatedMsg = 0;

} // anonymous namespace


// ============================================================================
// Forward declarations
// ============================================================================
static DWORD WINAPI RenderThreadProc(LPVOID param);
static void CALLBACK ExplorerCrashCallback(void* context, BOOLEAN timerOrWaitFired);
static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void CreateTrayIcon(HWND hwnd);
static void UpdateTrayIcon(bool paused);
static void ShowTrayMenu(HWND hwnd);
static void RemoveTrayIcon();
static void SignalPause();
static void SignalResume();

/// Helper: check if ANY system-level reason wants us paused
static bool ShouldBePaused()
{
    return g_pausedByBattery || g_pausedBySessionLock || g_pausedByIdle || g_pausedByUser;
}


// ============================================================================
// RenderThreadProc
// ============================================================================
static DWORD WINAPI RenderThreadProc(LPVOID /*param*/)
{
    LOG_INFO("Render thread started");

    // If no wallpaper is configured, wait for a change or shutdown signal
    if (g_config.wallpaperPath.empty()) {
        LOG_INFO("No wallpaper configured, render thread waiting for change event");
        HANDLE waitHandles[] = { g_hChangeEvent, g_hShutdownEvent };
        DWORD result = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0 + 1) {
            LOG_INFO("Render thread shutting down (no video was loaded)");
            return 0;
        }
    }

    // Initialize decoder using primary screen dimensions as fallback baseline context
    bool decoderReady = false;
    UINT screenWidth = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN));
    UINT screenHeight = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN));

    if (!g_config.wallpaperPath.empty()) {
        HRESULT hr = g_decoder.Init(g_renderer.GetDevice(), g_config.wallpaperPath.c_str(), screenWidth, screenHeight);
        if (SUCCEEDED(hr)) {
            decoderReady = true;
            g_timer.Start();
            LOG_INFO("Decoder initialized, render loop starting");
        } else {
            LOG_ERROR("Failed to initialize decoder (0x%08X)", static_cast<unsigned int>(hr));
        }
    }

    // Main render loop
    for (;;) {
        // Build the wait handle array based on decoder state
        HANDLE handles[5];
        DWORD  handleCount;

        if (decoderReady) {
            handles[0] = g_timer.GetHandle();
            handles[1] = g_hPauseEvent;
            handles[2] = g_hChangeEvent;
            handles[3] = g_hShutdownEvent;
            handles[4] = g_hRecoverEvent;
            handleCount = 5;
        } else {
            // No decoder — just wait for change, shutdown, or recover
            handles[0] = g_hChangeEvent;
            handles[1] = g_hShutdownEvent;
            handles[2] = g_hRecoverEvent;
            handleCount = 3;
        }

        DWORD result = WaitForMultipleObjects(handleCount, handles, FALSE, INFINITE);

        if (decoderReady) {
            switch (result) {
            // -----------------------------------------------------------------
            // Timer tick → decode one frame and present to all active monitors
            // -----------------------------------------------------------------
            case WAIT_OBJECT_0:
            {
                lw::Telemetry::Instance().StartFrame();

                ID3D11Texture2D* texture = nullptr;
                UINT subresource = 0;

                lw::FrameResult fr = g_decoder.ReadFrame(&texture, &subresource);

                if (fr == lw::FrameResult::EndOfStream) {
                    // Loop: seek back to start and continue
                    g_decoder.SeekToStart();
                    break;
                }

                if (fr == lw::FrameResult::Success && texture) {
                    lw::Telemetry::Instance().StartPresent();
                    HRESULT hr = g_renderer.PresentFrame(texture, subresource);
                    texture->Release();
                    lw::Telemetry::Instance().EndPresent();

                    // Check for device lost across physical displays
                    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
                        LOG_WARN("D3D11 device lost, attempting recovery across all screens");
                        g_timer.Stop();
                        g_decoder.Shutdown();
                        decoderReady = false;

                        std::vector<HWND> hwnds;
                        std::vector<HMONITOR> hMonitors;
                        for (size_t i = 0; i < g_host.GetMonitorCount(); ++i) {
                            hwnds.push_back(g_host.GetMonitorHWND(i));
                            hMonitors.push_back(g_host.GetMonitorHandle(i));
                        }

                        hr = g_renderer.HandleDeviceLost(hwnds, hMonitors, g_config.gpuPreference == "integrated");

                        if (SUCCEEDED(hr)) {
                            hr = g_decoder.Init(g_renderer.GetDevice(),
                                                g_config.wallpaperPath.c_str(),
                                                screenWidth,
                                                screenHeight);
                            if (SUCCEEDED(hr)) {
                                decoderReady = true;
                                g_timer.Start();
                                LOG_INFO("Device lost recovery successful across displays");
                            }
                        }
                        if (FAILED(hr)) {
                            LOG_ERROR("Device lost recovery failed (0x%08X)",
                                      static_cast<unsigned int>(hr));
                        }
                    }
                } else if (fr == lw::FrameResult::NoSample) {
                    lw::Telemetry::Instance().RecordDroppedFrame();
                }
                break;
            }

            // -----------------------------------------------------------------
            // Pause event → freeze on current frame until resume
            // -----------------------------------------------------------------
            case WAIT_OBJECT_0 + 1:
            {
                LOG_INFO("Render thread paused");
                g_timer.Stop();

                for (;;) {
                    // Block until resume, shutdown, change, or recover while paused
                    HANDLE resumeHandles[] = { g_hResumeEvent, g_hShutdownEvent, g_hChangeEvent, g_hRecoverEvent };
                    DWORD resumeResult = WaitForMultipleObjects(4, resumeHandles, FALSE, INFINITE);

                    if (resumeResult == WAIT_OBJECT_0) {
                        LOG_INFO("Render thread resumed");
                        if (decoderReady) {
                            g_timer.Start();
                        }
                        break;
                    }
                    else if (resumeResult == WAIT_OBJECT_0 + 1) {
                        LOG_INFO("Render thread shutting down from paused state");
                        goto shutdown;
                    }
                    else if (resumeResult == WAIT_OBJECT_0 + 2) {
                        LOG_INFO("Change event received while paused, rebuilding decoder...");
                        if (decoderReady) {
                            g_decoder.Shutdown();
                            decoderReady = false;
                        }
                        goto handle_change;
                    }
                    else if (resumeResult == WAIT_OBJECT_0 + 3) {
                        LOG_INFO("Recover event received while paused, rebuilding swapchains in paused state...");
                        g_renderer.ReleaseSwapChains();
                        
                        UINT baselineWidth = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN));
                        UINT baselineHeight = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN));

                        for (size_t i = 0; i < g_host.GetMonitorCount(); ++i) {
                            g_renderer.CreateSwapChainForMonitor(
                                g_host.GetMonitorHandle(i),
                                g_host.GetMonitorHWND(i),
                                baselineWidth,
                                baselineHeight
                            );
                        }
                        ResetEvent(g_hRecoverEvent);
                        LOG_INFO("All monitor swapchains successfully restored in paused state");
                    }
                }
                break;
            }

            // -----------------------------------------------------------------
            // Change event → reload video file
            // -----------------------------------------------------------------
            case WAIT_OBJECT_0 + 2:
                goto handle_change;

            // -----------------------------------------------------------------
            // Shutdown event → exit thread
            // -----------------------------------------------------------------
            case WAIT_OBJECT_0 + 3:
                goto shutdown;

            // -----------------------------------------------------------------
            // Recover event → recreate DXGI SwapChains safely
            // -----------------------------------------------------------------
            case WAIT_OBJECT_0 + 4:
                goto handle_recover;

            default:
                break;
            }
        } else {
            // No decoder active — waiting on change, shutdown, or recover
            switch (result) {
            case WAIT_OBJECT_0: // Change event
                goto handle_change;
            case WAIT_OBJECT_0 + 1: // Shutdown
                goto shutdown;
            case WAIT_OBJECT_0 + 2: // Recover event
                goto handle_recover;
            default:
                break;
            }
        }
        continue;

    handle_change:
        LOG_INFO("Video change event received");
        g_timer.Stop();

        if (decoderReady) {
            g_decoder.Shutdown();
            decoderReady = false;
        }

        if (!g_config.wallpaperPath.empty()) {
            HRESULT hr = g_decoder.Init(g_renderer.GetDevice(),
                                        g_config.wallpaperPath.c_str(),
                                        screenWidth,
                                        screenHeight);
            if (SUCCEEDED(hr)) {
                decoderReady = true;
                g_timer.Start();
                LOG_INFO("New video loaded successfully");
            } else {
                LOG_ERROR("Failed to load new video (0x%08X)",
                          static_cast<unsigned int>(hr));
            }
        } else {
            LOG_INFO("Wallpaper path cleared, decoder idle");
        }
        continue;

    handle_recover:
    {
        LOG_INFO("Recreation recover event received inside render thread");
        g_timer.Stop();
        
        g_renderer.ReleaseSwapChains();
        
        UINT baselineWidth = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN));
        UINT baselineHeight = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN));

        for (size_t i = 0; i < g_host.GetMonitorCount(); ++i) {
            g_renderer.CreateSwapChainForMonitor(
                g_host.GetMonitorHandle(i),
                g_host.GetMonitorHWND(i),
                baselineWidth,
                baselineHeight
            );
        }
        
        ResetEvent(g_hRecoverEvent);
        g_timer.Start();
        LOG_INFO("All monitor swapchains successfully restored in render context");
        continue;
    }

    shutdown:
        break;
    }

    // Clean up decoder and timer before exiting
    g_timer.Stop();
    if (decoderReady) {
        g_decoder.Shutdown();
    }

    LOG_INFO("Render thread exiting");
    return 0;
}


// ============================================================================
// ExplorerCrashCallback
//
// Called from background thread-pool. Shims recovery strictly to main GUI loop.
// ============================================================================
static void CALLBACK ExplorerCrashCallback(void* /*context*/, BOOLEAN /*timerOrWaitFired*/)
{
    LOG_WARN("Explorer.exe crash detected, posting crash notification to main thread...");
    PostMessageW(g_msgWnd, WM_EXPLORER_CRASHED, 0, 0);
}

// WinEvent hook callback to trigger fullscreen bounds check
void CALLBACK WinEventProc(
    HWINEVENTHOOK /*hWinEventHook*/,
    DWORD /*event*/,
    HWND /*hwnd*/,
    LONG idObject,
    LONG idChild,
    DWORD /*dwEventThread*/,
    DWORD /*dwmsEventTime*/
) {
    if (idObject == OBJID_WINDOW && idChild == CHILDID_SELF) {
        // Enqueue a non-blocking check to execute on GUI thread
        PostMessageW(g_msgWnd, WM_USER_CHECK_FULLSCREEN, 0, 0);
    }
}


// ============================================================================
// SettingsDlgProc
// ============================================================================
static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (msg) {
    case WM_INITDIALOG:
    {
        SetDlgItemTextW(hDlg, IDC_WALLPAPER_PATH, g_config.wallpaperPath.c_str());

        HWND hSlider = GetDlgItem(hDlg, IDC_FPS_SLIDER);
        SendMessageW(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, 60));
        SendMessageW(hSlider, TBM_SETPOS, TRUE, g_config.fps);
        SendMessageW(hSlider, TBM_SETTICFREQ, 6, 0);

        wchar_t fpsLabel[16];
        wsprintfW(fpsLabel, L"%d FPS", g_config.fps);
        SetDlgItemTextW(hDlg, IDC_FPS_LABEL, fpsLabel);

        SetDlgItemInt(hDlg, IDC_IDLE_TIMEOUT, g_config.idleTimeoutMinutes, FALSE);

        CheckDlgButton(hDlg, IDC_PAUSE_BATTERY,
                        g_config.pauseOnBattery ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_PAUSE_FULLSCREEN,
                        g_config.pauseOnFullscreen ? BST_CHECKED : BST_UNCHECKED);

        HWND hGpuCombo = GetDlgItem(hDlg, IDC_GPU_COMBO);
        SendMessageW(hGpuCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Integrated"));
        SendMessageW(hGpuCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Discrete"));
        SendMessageW(hGpuCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto"));

        int gpuIndex = 0;
        if (g_config.gpuPreference == "discrete") gpuIndex = 1;
        else if (g_config.gpuPreference == "auto") gpuIndex = 2;
        SendMessageW(hGpuCombo, CB_SETCURSEL, gpuIndex, 0);

        return TRUE;
    }

    case WM_HSCROLL:
    {
        HWND hSlider = GetDlgItem(hDlg, IDC_FPS_SLIDER);
        if (reinterpret_cast<HWND>(lParam) == hSlider) {
            int pos = static_cast<int>(SendMessageW(hSlider, TBM_GETPOS, 0, 0));
            wchar_t fpsLabel[16];
            wsprintfW(fpsLabel, L"%d FPS", pos);
            SetDlgItemTextW(hDlg, IDC_FPS_LABEL, fpsLabel);
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BROWSE_BTN:
        {
            wchar_t filePath[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hDlg;
            ofn.lpstrFilter = L"MP4 Video Files\0*.mp4\0All Files\0*.*\0";
            ofn.lpstrFile   = filePath;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            ofn.lpstrTitle  = L"Select Wallpaper Video";

            if (GetOpenFileNameW(&ofn)) {
                SetDlgItemTextW(hDlg, IDC_WALLPAPER_PATH, filePath);
            }
            return TRUE;
        }

        case IDC_APPLY_BTN:
        {
            wchar_t pathBuf[MAX_PATH] = {};
            GetDlgItemTextW(hDlg, IDC_WALLPAPER_PATH, pathBuf, MAX_PATH);
            bool pathChanged = (g_config.wallpaperPath != pathBuf);
            g_config.wallpaperPath = pathBuf;

            HWND hSlider = GetDlgItem(hDlg, IDC_FPS_SLIDER);
            int newFps = static_cast<int>(SendMessageW(hSlider, TBM_GETPOS, 0, 0));
            if (newFps < 1)  newFps = 1;
            if (newFps > 60) newFps = 60;

            bool fpsChanged = (newFps != g_config.fps);
            g_config.fps = newFps;

            BOOL translated = FALSE;
            int idleTimeout = GetDlgItemInt(hDlg, IDC_IDLE_TIMEOUT, &translated, FALSE);
            if (translated) {
                if (idleTimeout < 0)  idleTimeout = 0;
                if (idleTimeout > 60) idleTimeout = 60;
                g_config.idleTimeoutMinutes = idleTimeout;
            }

            g_config.pauseOnBattery =
                (IsDlgButtonChecked(hDlg, IDC_PAUSE_BATTERY) == BST_CHECKED);
            g_config.pauseOnFullscreen =
                (IsDlgButtonChecked(hDlg, IDC_PAUSE_FULLSCREEN) == BST_CHECKED);

            HWND hGpuCombo = GetDlgItem(hDlg, IDC_GPU_COMBO);
            int gpuSel = static_cast<int>(SendMessageW(hGpuCombo, CB_GETCURSEL, 0, 0));
            switch (gpuSel) {
                case 0:  g_config.gpuPreference = "integrated"; break;
                case 1:  g_config.gpuPreference = "discrete";   break;
                case 2:  g_config.gpuPreference = "auto";        break;
                default: g_config.gpuPreference = "integrated";  break;
            }

            lw::SaveConfig(g_config);

            if (fpsChanged) {
                g_timer.ChangeFPS(g_config.fps);
            }

            if (pathChanged) {
                g_pausedByUser = false;
                SignalResume();
            }

            SetEvent(g_hChangeEvent);

            // Re-trigger an immediate event-driven bounds check
            PostMessageW(g_msgWnd, WM_USER_CHECK_FULLSCREEN, 0, 0);

            EndDialog(hDlg, IDOK);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }

    return FALSE;
}


// ============================================================================
// Tray icon management
// ============================================================================
static void CreateTrayIcon(HWND hwnd)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;

    g_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!g_nid.hIcon) {
        g_nid.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    }

    wcscpy_s(g_nid.szTip, L"LiveWallpaper — Playing");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    LOG_INFO("Tray icon created");
}

static void UpdateTrayIcon(bool paused)
{
    if (paused) {
        wcscpy_s(g_nid.szTip, L"LiveWallpaper — Paused");
    } else {
        if (!g_config.wallpaperPath.empty()) {
            const wchar_t* filename = g_config.wallpaperPath.c_str();
            const wchar_t* lastSlash = wcsrchr(filename, L'\\');
            if (lastSlash) filename = lastSlash + 1;

            wchar_t tip[128];
            wsprintfW(tip, L"LiveWallpaper — %s at %d FPS", filename, g_config.fps);
            wcsncpy_s(g_nid.szTip, tip, _TRUNCATE);
        } else {
            wcscpy_s(g_nid.szTip, L"LiveWallpaper — No wallpaper");
        }
    }

    g_nid.uFlags = NIF_TIP | NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void ShowTrayMenu(HWND hwnd)
{
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    if (g_isPaused) {
        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_RESUME, L"Resume");
    } else {
        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_PAUSE, L"Pause");
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_CHANGE,   L"Change Wallpaper...");
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SETTINGS, L"Settings...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT,     L"Exit");

    SetForegroundWindow(hwnd);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

static void RemoveTrayIcon()
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_nid.hIcon) {
        DestroyIcon(g_nid.hIcon);
        g_nid.hIcon = nullptr;
    }
    LOG_INFO("Tray icon removed");
}


// ============================================================================
// Signal helpers — centralize pause/resume logic
// ============================================================================
static void SignalPause()
{
    if (!g_isPaused) {
        g_isPaused = true;
        ResetEvent(g_hResumeEvent);
        SetEvent(g_hPauseEvent);
        UpdateTrayIcon(true);
        LOG_INFO("Signaling render thread to pause");
    }
}

static void SignalResume()
{
    if (g_isPaused && !ShouldBePaused()) {
        g_isPaused = false;
        ResetEvent(g_hPauseEvent);
        SetEvent(g_hResumeEvent);
        UpdateTrayIcon(false);
        LOG_INFO("Signaling render thread to resume");
    }
}


// ============================================================================
// WndProc — Message-only window procedure
// ============================================================================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_uTaskbarCreatedMsg && g_uTaskbarCreatedMsg != 0) {
        LOG_INFO("Taskbar recreated (explorer.exe restarted), restoring system tray icon...");
        CreateTrayIcon(hwnd);
        UpdateTrayIcon(g_isPaused);
        return 0;
    }

    switch (msg) {

    // Thread-safe shell recovery triggered on the main thread loop
    case WM_EXPLORER_CRASHED:
        LOG_INFO("Main thread received crash notification, setting 2-second shell stabilization timer...");
        SetTimer(hwnd, IDT_RECOVER_SHELL, 2000, nullptr);
        return 0;

    // Event-driven bounds evaluation check
    case WM_USER_CHECK_FULLSCREEN:
        if (g_config.pauseOnFullscreen) {
            // Check for fullscreen applications on each display independently
            for (size_t i = 0; i < g_host.GetMonitorCount(); ++i) {
                bool monitorFullscreen = lw::PowerMonitor::IsFullscreenAppOnMonitor(g_host.GetMonitorHandle(i));
                g_renderer.SetMonitorPauseState(g_host.GetMonitorHandle(i), monitorFullscreen);
                g_host.SetMonitorPauseState(i, monitorFullscreen);
            }
            
            // If ALL displays are covered by fullscreen apps, tell render thread to sleep completely
            bool allPaused = true;
            for (size_t i = 0; i < g_host.GetMonitorCount(); ++i) {
                if (!g_host.IsMonitorPaused(i)) {
                    allPaused = false;
                    break;
                }
            }

            if (allPaused) {
                SignalPause();
            } else {
                SignalResume();
            }
        }
        return 0;

    case WM_TRAYICON:
        switch (lParam) {
        case WM_LBUTTONUP:
            if (g_isPaused) {
                g_pausedByUser = false;
                SignalResume();
            } else {
                g_pausedByUser = true;
                SignalPause();
            }
            break;

        case WM_RBUTTONUP:
            ShowTrayMenu(hwnd);
            break;
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_TRAY_PAUSE:
            g_pausedByUser = true;
            SignalPause();
            break;

        case IDM_TRAY_RESUME:
            g_pausedByUser = false;
            SignalResume();
            break;

        case IDM_TRAY_CHANGE:
        {
            wchar_t filePath[MAX_PATH] = {};
            OPENFILENAMEW ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hwnd;
            ofn.lpstrFilter = L"MP4 Video Files\0*.mp4\0All Files\0*.*\0";
            ofn.lpstrFile   = filePath;
            ofn.nMaxFile    = MAX_PATH;
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            ofn.lpstrTitle  = L"Select Wallpaper Video";

            if (GetOpenFileNameW(&ofn)) {
                g_config.wallpaperPath = filePath;
                lw::SaveConfig(g_config);
                g_pausedByUser = false;
                SignalResume();
                SetEvent(g_hChangeEvent);
                UpdateTrayIcon(g_isPaused);
                LOG_INFO("Wallpaper changed via tray menu");
            }
            break;
        }

        case IDM_TRAY_SETTINGS:
            DialogBoxW(GetModuleHandleW(nullptr),
                        MAKEINTRESOURCEW(IDD_SETTINGS),
                        hwnd,
                        SettingsDlgProc);
            break;

        case IDM_TRAY_EXIT:
            LOG_INFO("Exit requested from tray menu");
            DestroyWindow(hwnd);
            break;
        }
        return 0;

    // Modern Session lock/unlock notifications (Zero polling)
    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK) {
            LOG_INFO("Session locked — pausing rendering globally");
            g_pausedBySessionLock = true;
            SignalPause();
        } else if (wParam == WTS_SESSION_UNLOCK) {
            LOG_INFO("Session unlocked — resuming if clear");
            g_pausedBySessionLock = false;
            SignalResume();
        }
        return 0;

    // Modern Power setting transitions (Zero polling)
    case WM_POWERBROADCAST:
        if (msg == WM_POWERBROADCAST && wParam == PBT_POWERSETTINGCHANGE) {
            POWERBROADCAST_SETTING* pbs = reinterpret_cast<POWERBROADCAST_SETTING*>(lParam);
            if (pbs && pbs->PowerSetting == GUID_ACDC_POWER_SOURCE && pbs->DataLength == sizeof(DWORD)) {
                DWORD powerSource = *reinterpret_cast<DWORD*>(pbs->Data);
                // 0 = Battery, 1 = AC
                bool onBattery = (powerSource == 0);

                if (onBattery && g_config.pauseOnBattery) {
                    g_pausedByBattery = true;
                    SignalPause();
                    LOG_INFO("Power transition: On battery — pausing wallpaper");
                } else {
                    g_pausedByBattery = false;
                    SignalResume();
                    LOG_INFO("Power transition: On AC power — resuming if clear");
                }
            }
        }
        return TRUE;

    // Display Orientation/Topology change
    case WM_DISPLAYCHANGE:
        LOG_INFO("Display change detected (%dx%d), rebuilding monitor layout...", LOWORD(lParam), HIWORD(lParam));
        if (SUCCEEDED(g_host.Recover())) {
            SetEvent(g_hRecoverEvent);
        }
        return 0;

    // Periodic timers (Idle detection + shell stabilization)
    case WM_TIMER:
        switch (wParam) {
        // Safe delayed shell recovery
        case IDT_RECOVER_SHELL:
            KillTimer(hwnd, IDT_RECOVER_SHELL);
            LOG_INFO("Shell stabilization delay elapsed. Re-injecting...");
            if (SUCCEEDED(g_host.Recover())) {
                g_host.StartExplorerWatch(ExplorerCrashCallback, nullptr);
                SetEvent(g_hRecoverEvent);
                LOG_INFO("Main thread recovery procedure complete.");
            } else {
                LOG_ERROR("Recovery failed, rescheduling recovery in 5 seconds...");
                SetTimer(hwnd, IDT_RECOVER_SHELL, 5000, nullptr);
            }
            break;

        // User inactivity check — every 30 seconds (low frequency)
        case IDT_IDLE_CHECK:
            if (g_config.idleTimeoutMinutes > 0) {
                DWORD timeoutMs = static_cast<DWORD>(g_config.idleTimeoutMinutes) * 60 * 1000;
                bool idle = lw::PowerMonitor::IsUserIdle(timeoutMs);

                if (idle && !g_pausedByIdle) {
                    g_pausedByIdle = true;
                    SignalPause();
                    LOG_INFO("User idle for %d+ minutes, pausing globally", g_config.idleTimeoutMinutes);
                } else if (!idle && g_pausedByIdle) {
                    g_pausedByIdle = false;
                    SignalResume();
                    LOG_INFO("User activity detected, resuming globally if clear");
                }
            }
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


// ============================================================================
// WinMain — Application entry point
// ============================================================================
int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE /*hPrevInstance*/,
    _In_ LPWSTR lpCmdLine,
    _In_ int /*nCmdShow*/)
{
    // Initialize common controls for Trackbar and other standard controls
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC  = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    // Register taskbar creation message for explorer crash recovery of system tray icon
    g_uTaskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    // Step 1: Single-instance mutex
    g_instanceMutex = CreateMutexW(nullptr, TRUE, L"LiveWallpaper_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_instanceMutex) CloseHandle(g_instanceMutex);
        return 0;
    }

    // Step 2: DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Step 3: COM and Media Foundation initialization
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        CloseHandle(g_instanceMutex);
        return 1;
    }

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        CoUninitialize();
        CloseHandle(g_instanceMutex);
        return 1;
    }

    LOG_INFO("LiveWallpaper starting up with event-driven stabilization");

    // Step 4: Load configuration
    const wchar_t* configOverride    = nullptr;
    const wchar_t* wallpaperOverride = nullptr;

    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(lpCmdLine, &argc);
        if (argv) {
            for (int i = 0; i < argc; ++i) {
                if (wcscmp(argv[i], L"--config") == 0 && i + 1 < argc) {
                    configOverride = argv[i + 1];
                    ++i;
                } else if (wcscmp(argv[i], L"--wallpaper") == 0 && i + 1 < argc) {
                    wallpaperOverride = argv[i + 1];
                    ++i;
                }
            }

            g_config = lw::LoadConfig(configOverride);

            if (wallpaperOverride) {
                g_config.wallpaperPath = wallpaperOverride;
            }

            LocalFree(argv);
        } else {
            g_config = lw::LoadConfig();
        }
    }

    LOG_INFO("Config loaded: fps=%d, idle=%dmin, battery=%s, fullscreen=%s",
             g_config.fps, g_config.idleTimeoutMinutes,
             g_config.pauseOnBattery ? "pause" : "ignore",
             g_config.pauseOnFullscreen ? "pause" : "ignore");

    // Step 5: Create inter-thread events
    g_hPauseEvent    = CreateEventW(nullptr, TRUE,  FALSE, nullptr); // Manual-reset
    g_hResumeEvent   = CreateEventW(nullptr, TRUE,  FALSE, nullptr); // Manual-reset
    g_hChangeEvent   = CreateEventW(nullptr, FALSE, FALSE, nullptr); // Auto-reset
    g_hShutdownEvent = CreateEventW(nullptr, TRUE,  FALSE, nullptr); // Manual-reset
    g_hRecoverEvent  = CreateEventW(nullptr, FALSE, FALSE, nullptr); // Auto-reset

    if (!g_hPauseEvent || !g_hResumeEvent || !g_hChangeEvent || !g_hShutdownEvent || !g_hRecoverEvent) {
        LOG_ERROR("Failed to create synchronization events");
        MFShutdown();
        CoUninitialize();
        CloseHandle(g_instanceMutex);
        return 1;
    }

    // Step 6: Initialize the D3D11 renderer
    bool preferIntegrated = (g_config.gpuPreference == "integrated" ||
                             g_config.gpuPreference == "auto");

    hr = g_renderer.InitDevice(preferIntegrated);
    if (FAILED(hr)) {
        LOG_ERROR("Renderer InitDevice failed (0x%08X)", static_cast<unsigned int>(hr));
        goto cleanup_events;
    }

    // Step 7: Initialize the WallpaperHost (WorkerW injection)
    hr = g_host.Init();
    if (FAILED(hr)) {
        LOG_ERROR("WallpaperHost Init failed (0x%08X)", static_cast<unsigned int>(hr));
        g_renderer.Shutdown();
        goto cleanup_events;
    }

    // Step 8: Create DXGI SwapChains for each detected display HWND
    {
        UINT baselineWidth = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN));
        UINT baselineHeight = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN));

        for (size_t i = 0; i < g_host.GetMonitorCount(); ++i) {
            hr = g_renderer.CreateSwapChainForMonitor(
                g_host.GetMonitorHandle(i),
                g_host.GetMonitorHWND(i),
                baselineWidth,
                baselineHeight
            );
            if (FAILED(hr)) {
                LOG_ERROR("CreateSwapChainForMonitor failed at index %zu (0x%08X)", i, static_cast<unsigned int>(hr));
                g_host.Shutdown();
                g_renderer.Shutdown();
                goto cleanup_events;
            }
        }
    }

    // Step 9: Initialize the frame timer
    hr = g_timer.Init(g_config.fps);
    if (FAILED(hr)) {
        LOG_ERROR("FrameTimer Init failed (0x%08X)", static_cast<unsigned int>(hr));
        g_host.Shutdown();
        g_renderer.Shutdown();
        goto cleanup_events;
    }

    // Step 10: Battery check on startup
    if (g_config.pauseOnBattery && lw::PowerMonitor::IsOnBattery()) {
        g_pausedByBattery = true;
        g_isPaused = true;
        SetEvent(g_hPauseEvent);
        LOG_INFO("Starting paused (on battery)");
    }

    // Step 11: Create the render thread
    g_renderThread = CreateThread(nullptr, 0, RenderThreadProc, nullptr, 0, nullptr);
    if (!g_renderThread) {
        LOG_ERROR("Failed to create render thread");
        g_timer.Shutdown();
        g_host.Shutdown();
        g_renderer.Shutdown();
        goto cleanup_events;
    }

    // Step 12: Register the message-only window class and create the window
    {
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance      = hInstance;
        wc.lpszClassName  = kMsgWndClass;

        if (!RegisterClassExW(&wc)) {
            LOG_ERROR("RegisterClassExW failed for message window");
            SetEvent(g_hShutdownEvent);
            SetEvent(g_hResumeEvent);
            WaitForSingleObject(g_renderThread, 5000);
            g_timer.Shutdown();
            g_host.Shutdown();
            g_renderer.Shutdown();
            goto cleanup_events;
        }

        g_msgWnd = CreateWindowExW(
            0, kMsgWndClass, L"LiveWallpaper",
            0, 0, 0, 0, 0,
            nullptr, // Top-level hidden window (allows tray icon and system broadcasts)
            nullptr,
            hInstance,
            nullptr);

        if (!g_msgWnd) {
            LOG_ERROR("Failed to create message-only window");
            SetEvent(g_hShutdownEvent);
            SetEvent(g_hResumeEvent);
            WaitForSingleObject(g_renderThread, 5000);
            g_timer.Shutdown();
            g_host.Shutdown();
            g_renderer.Shutdown();
            goto cleanup_events;
        }
    }

    // Step 13: Create tray icon
    CreateTrayIcon(g_msgWnd);
    UpdateTrayIcon(g_isPaused);

    // Step 14: Register for Event-driven OS alerts (No Polling)
    WTSRegisterSessionNotification(g_msgWnd, NOTIFY_FOR_THIS_SESSION);
    g_hPowerNotify = RegisterPowerSettingNotification(g_msgWnd, &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);

    // Step 15: Register WinEvent Hooks to track foreground changes, moves, and minimizes
    g_hEventHookForeground = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT
    );
    g_hEventHookMoveSize = SetWinEventHook(
        EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND,
        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT
    );
    g_hEventHookMinimize = SetWinEventHook(
        EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT
    );

    // Low-frequency 30s timer ONLY for user inactivity checks
    SetTimer(g_msgWnd, IDT_IDLE_CHECK, 30000, nullptr);

    // Step 16: Start explorer.exe crash watch
    hr = g_host.StartExplorerWatch(ExplorerCrashCallback, nullptr);
    if (FAILED(hr)) {
        LOG_WARN("Failed to start explorer watch (0x%08X) — crash recovery disabled",
                 static_cast<unsigned int>(hr));
    }

    // Perform an initial bounds check on startup to set correct state
    PostMessageW(g_msgWnd, WM_USER_CHECK_FULLSCREEN, 0, 0);

    LOG_INFO("Startup complete, entering message pump");

    // Step 17: Win32 message pump
    {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // Step 18: Clean shutdown
    LOG_INFO("Shutting down...");

    // Unhook WinEvent listeners
    if (g_hEventHookForeground) UnhookWinEvent(g_hEventHookForeground);
    if (g_hEventHookMoveSize)   UnhookWinEvent(g_hEventHookMoveSize);
    if (g_hEventHookMinimize)   UnhookWinEvent(g_hEventHookMinimize);

    // Unregister power alerts
    if (g_hPowerNotify) {
        UnregisterPowerSettingNotification(g_hPowerNotify);
    }

    // Clean up WTS notifications and timers
    if (g_msgWnd) {
        WTSUnRegisterSessionNotification(g_msgWnd);
        KillTimer(g_msgWnd, IDT_IDLE_CHECK);
        RemoveTrayIcon();
        DestroyWindow(g_msgWnd);
    }

    // Signal render thread to exit
    SetEvent(g_hShutdownEvent);
    SetEvent(g_hResumeEvent); // Bypass pause waits

    if (g_renderThread) {
        DWORD waitResult = WaitForSingleObject(g_renderThread, 5000);
        if (waitResult == WAIT_TIMEOUT) {
            LOG_WARN("Render thread timed out, terminating...");
            TerminateThread(g_renderThread, 1);
        }
        CloseHandle(g_renderThread);
        g_renderThread = nullptr;
    }

    // Shutdown subsystems in reverse order
    g_timer.Shutdown();
    g_host.Shutdown();
    g_renderer.Shutdown();

    UnregisterClassW(kMsgWndClass, hInstance);

cleanup_events:
    if (g_hPauseEvent)    { CloseHandle(g_hPauseEvent);    g_hPauseEvent    = nullptr; }
    if (g_hResumeEvent)   { CloseHandle(g_hResumeEvent);   g_hResumeEvent   = nullptr; }
    if (g_hChangeEvent)   { CloseHandle(g_hChangeEvent);   g_hChangeEvent   = nullptr; }
    if (g_hShutdownEvent) { CloseHandle(g_hShutdownEvent); g_hShutdownEvent = nullptr; }
    if (g_hRecoverEvent)  { CloseHandle(g_hRecoverEvent);  g_hRecoverEvent  = nullptr; }

    MFShutdown();
    CoUninitialize();

    if (g_instanceMutex) {
        ReleaseMutex(g_instanceMutex);
        CloseHandle(g_instanceMutex);
        g_instanceMutex = nullptr;
    }

    LOG_INFO("Shutdown complete");
    return 0;
}
