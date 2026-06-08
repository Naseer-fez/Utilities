// =============================================================================
// wallpaper_host.cpp — WorkerW injection, render window, and explorer recovery
// =============================================================================

#include "wallpaper_host.h"
#include <algorithm>

namespace lw {

// Window class name for our render surfaces
static constexpr wchar_t kRenderClassName[] = L"LiveWallpaperRender";

// File-scope atom for the registered window class
static ATOM s_windowClassAtom = 0;

// Context structure for WorkerW enumeration
struct WorkerWContext {
    DWORD explorerPid = 0;
    HWND shellDefViewParent = nullptr;
    HWND wallpaperWorkerW = nullptr;
};

// Callback to find the target WorkerW
static BOOL CALLBACK FindWorkerWCallback(HWND hwnd, LPARAM lParam) {
    WorkerWContext* ctx = reinterpret_cast<WorkerWContext*>(lParam);

    wchar_t className[256];
    if (GetClassNameW(hwnd, className, 256) && wcscmp(className, L"WorkerW") == 0) {
        // Only check WorkerW windows belonging to explorer.exe
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == ctx->explorerPid) {
            HWND defView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
            if (defView) {
                ctx->shellDefViewParent = hwnd;
            } else {
                // It's a WorkerW without SHELLDLL_DefView. Verify it covers the primary screen area to avoid tiny utility WorkerWs.
                if (!ctx->wallpaperWorkerW) {
                    RECT rc = {};
                    if (GetWindowRect(hwnd, &rc)) {
                        int w = rc.right - rc.left;
                        int h = rc.bottom - rc.top;
                        if (w >= GetSystemMetrics(SM_CXSCREEN) / 2 && h >= GetSystemMetrics(SM_CYSCREEN) / 2) {
                            ctx->wallpaperWorkerW = hwnd;
                        }
                    }
                }
            }
        }
    }
    return TRUE; // Continue enumerating
}

// Context structure for display enumeration window creation
struct EnumMonitorsContext {
    std::vector<MonitorOutput>* monitors;
    HWND parentWorkerW;
    HINSTANCE hInst;
};

// Callback to enumerate displays and create a child window on each
static BOOL CALLBACK EnumMonitorsProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM lParam) {
    EnumMonitorsContext* ctx = reinterpret_cast<EnumMonitorsContext*>(lParam);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMonitor, &mi)) {
        MonitorOutput mo = {};
        mo.hMonitor = hMonitor;
        mo.rect = mi.rcMonitor;
        mo.deviceName = mi.szDevice;
        mo.isPaused = false;

        // Make coordinates relative to the parent WorkerW client area
        RECT parentRect = {};
        GetWindowRect(ctx->parentWorkerW, &parentRect);
        int x = mo.rect.left - parentRect.left;
        int y = mo.rect.top - parentRect.top;
        int w = mo.rect.right - mo.rect.left;
        int h = mo.rect.bottom - mo.rect.top;

        // Create child window in the WorkerW spanning exactly this monitor's bounds
        mo.renderWnd = CreateWindowExW(
            0,
            kRenderClassName,
            L"LiveWallpaper",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            x,
            y,
            w,
            h,
            ctx->parentWorkerW,
            nullptr,
            ctx->hInst,
            nullptr
        );

        if (mo.renderWnd) {
            ShowWindow(mo.renderWnd, SW_SHOW);
            UpdateWindow(mo.renderWnd);
            ctx->monitors->push_back(mo);
            LOG_INFO("Created render window 0x%p for monitor %ls [%d, %d, %d, %d]",
                     mo.renderWnd, mo.deviceName.c_str(),
                     mo.rect.left, mo.rect.top, mo.rect.right, mo.rect.bottom);
        } else {
            LOG_ERROR("Failed to create render window for monitor %ls, error=%u", mo.deviceName.c_str(), GetLastError());
        }
    }
    return TRUE;
}

// Minimal window procedure for our child render windows
static LRESULT CALLBACK RenderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1; // Suppress erase background to prevent flicker
        case WM_CLOSE:
            return 0; // Prevent close commands from child
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

WallpaperHost::~WallpaperHost() {
    Shutdown();
}

void WallpaperHost::LogShellHierarchy() {
    LOG_INFO("=== Shell Window Hierarchy Diagnostics ===");
    HWND progman = FindWindowW(L"Progman", nullptr);
    LOG_INFO("Progman HWND: 0x%p", progman);
    
    DWORD explorerPid = 0;
    GetWindowThreadProcessId(progman, &explorerPid);
    LOG_INFO("Explorer PID: %u", explorerPid);
    
    struct EnumCtx {
        DWORD pid;
    } ctx = { explorerPid };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        EnumCtx* pCtx = reinterpret_cast<EnumCtx*>(lParam);
        wchar_t className[256] = {};
        if (GetClassNameW(hwnd, className, 256)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == pCtx->pid) {
                wchar_t title[256] = {};
                GetWindowTextW(hwnd, title, 256);
                HWND parent = GetParent(hwnd);
                HWND owner = GetWindow(hwnd, GW_OWNER);
                BOOL visible = IsWindowVisible(hwnd);
                LONG style = GetWindowLongW(hwnd, GWL_STYLE);
                RECT rect = {};
                GetWindowRect(hwnd, &rect);
                HWND childDefView = FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
                
                LOG_INFO("Window 0x%p | Class: %ls | Title: %ls | Parent: 0x%p | Owner: 0x%p | Visible: %s | Style: 0x%08X | Rect: [%d, %d, %d, %d] | Has DefView: %s",
                         hwnd, className, title, parent, owner, visible ? "Yes" : "No", style,
                         rect.left, rect.top, rect.right, rect.bottom, childDefView ? "Yes" : "No");
            }
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    LOG_INFO("==========================================");
}

HRESULT WallpaperHost::Init() {
    // Find or spawn the WorkerW container window
    HRESULT hr = FindWorkerW();
    if (FAILED(hr)) return hr;

    // Register our child window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_OWNDC;
    wc.lpfnWndProc   = RenderWndProc;
    wc.hInstance      = GetModuleHandleW(nullptr);
    wc.lpszClassName  = kRenderClassName;

    // Unregister first in case of stale residue from crashes
    UnregisterClassW(kRenderClassName, wc.hInstance);

    s_windowClassAtom = RegisterClassExW(&wc);
    if (!s_windowClassAtom) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            LOG_ERROR("WallpaperHost::Init — RegisterClassExW failed, error=%u", err);
            return HRESULT_FROM_WIN32(err);
        }
    }

    // Enumerate monitors and create a child render window inside WorkerW for each
    EnumMonitorsContext ctx = { &m_monitors, m_workerW, GetModuleHandleW(nullptr) };
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsProc, reinterpret_cast<LPARAM>(&ctx));

    if (m_monitors.empty()) {
        LOG_ERROR("WallpaperHost::Init — Failed to create any render windows on active monitors");
        return E_FAIL;
    }

    return S_OK;
}

HRESULT WallpaperHost::FindWorkerW() {
    m_progman = FindWindowW(L"Progman", nullptr);
    if (!m_progman) {
        LOG_ERROR("WallpaperHost::FindWorkerW — Progman not found");
        return E_FAIL;
    }

    LOG_INFO("WallpaperHost::FindWorkerW — Found Progman (0x%p)", m_progman);

    // Send undocumented message 0x052C to Progman to spawn WorkerW (using standard 0x0000000D parameter)
    DWORD_PTR result = 0;
    SendMessageTimeoutW(m_progman, 0x052C, 0x0000000D, 0, SMTO_NORMAL, 1000, &result);

    WorkerWContext ctx = {};
    GetWindowThreadProcessId(m_progman, &ctx.explorerPid);

    // Retry loop using MsgWaitForMultipleObjectsEx to let Explorer complete the tree transition safely
    for (int i = 0; i < 20; ++i) {
        ctx.shellDefViewParent = nullptr;
        ctx.wallpaperWorkerW = nullptr;
        EnumWindows(FindWorkerWCallback, reinterpret_cast<LPARAM>(&ctx));

        // Break if we successfully found the WorkerW housing SHELLDLL_DefView
        if (ctx.shellDefViewParent != nullptr && ctx.shellDefViewParent != m_progman) {
            break;
        }

        MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }

    // Print full diagnostics before identifying adjacent window
    LogShellHierarchy();

    HWND baseWindow = nullptr;
    if (ctx.shellDefViewParent) {
        baseWindow = ctx.shellDefViewParent;
        LOG_INFO("WallpaperHost::FindWorkerW — using shellDefViewParent (0x%p) as base window", baseWindow);
    } else {
        HWND defViewInProgman = FindWindowExW(m_progman, nullptr, L"SHELLDLL_DefView", nullptr);
        if (defViewInProgman) {
            baseWindow = m_progman;
            LOG_INFO("WallpaperHost::FindWorkerW — SHELLDLL_DefView inside Progman, using Progman (0x%p)", baseWindow);
        }
    }

    // The target WorkerW is the sibling immediately below/above the base window in Z-order
    HWND siblingWorker = nullptr;
    if (baseWindow) {
        // Search next siblings
        HWND nextWnd = GetWindow(baseWindow, GW_HWNDNEXT);
        while (nextWnd) {
            wchar_t className[256] = {};
            if (GetClassNameW(nextWnd, className, 256) && wcscmp(className, L"WorkerW") == 0) {
                DWORD pid = 0;
                GetWindowThreadProcessId(nextWnd, &pid);
                if (pid == ctx.explorerPid) {
                    RECT rc = {};
                    if (GetWindowRect(nextWnd, &rc)) {
                        int w = rc.right - rc.left;
                        int h = rc.bottom - rc.top;
                        // Avoid small utility windows (e.g. 166x47 widget/thumbnail containers)
                        if (w >= GetSystemMetrics(SM_CXSCREEN) / 2 && h >= GetSystemMetrics(SM_CYSCREEN) / 2) {
                            siblingWorker = nextWnd;
                            break;
                        }
                    }
                }
            }
            nextWnd = GetWindow(nextWnd, GW_HWNDNEXT);
        }

        // Search previous siblings (above baseWindow in Z-order)
        if (!siblingWorker) {
            HWND prevWnd = GetWindow(baseWindow, GW_HWNDPREV);
            while (prevWnd) {
                wchar_t className[256] = {};
                if (GetClassNameW(prevWnd, className, 256) && wcscmp(className, L"WorkerW") == 0) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(prevWnd, &pid);
                    if (pid == ctx.explorerPid) {
                        RECT rc = {};
                        if (GetWindowRect(prevWnd, &rc)) {
                            int w = rc.right - rc.left;
                            int h = rc.bottom - rc.top;
                            // Avoid small utility windows (e.g. 166x47 widget/thumbnail containers)
                            if (w >= GetSystemMetrics(SM_CXSCREEN) / 2 && h >= GetSystemMetrics(SM_CYSCREEN) / 2) {
                                siblingWorker = prevWnd;
                                break;
                            }
                        }
                    }
                }
                prevWnd = GetWindow(prevWnd, GW_HWNDPREV);
            }
        }
    }

    if (siblingWorker) {
        m_workerW = siblingWorker;
        LOG_INFO("WallpaperHost::FindWorkerW — selected sibling WorkerW (0x%p)", m_workerW);
    } else if (ctx.wallpaperWorkerW) {
        m_workerW = ctx.wallpaperWorkerW;
        LOG_INFO("WallpaperHost::FindWorkerW — using standalone WorkerW fallback (0x%p)", m_workerW);
    }

    if (!m_workerW) {
        LOG_ERROR("WallpaperHost::FindWorkerW — Target WorkerW not found");
        return E_FAIL;
    }

    // Force parent WorkerW visibility to ensure child render windows are visible
    ShowWindow(m_workerW, SW_SHOW);
    UpdateWindow(m_workerW);

    return S_OK;
}

HRESULT WallpaperHost::Recover() {
    LOG_INFO("WallpaperHost::Recover — re-injecting and rebuilding displays after explorer restart");

    // Clean up all dead child windows and handles recursively
    for (auto& m : m_monitors) {
        if (m.renderWnd && IsWindow(m.renderWnd)) {
            DestroyWindow(m.renderWnd);
        }
    }
    m_monitors.clear();

    // Re-locate the new WorkerW spawned by the restarted Explorer shell
    HRESULT hr = FindWorkerW();
    if (FAILED(hr)) {
        LOG_ERROR("WallpaperHost::Recover — FindWorkerW failed");
        return hr;
    }

    // Re-create the render windows for all active monitors
    EnumMonitorsContext ctx = { &m_monitors, m_workerW, GetModuleHandleW(nullptr) };
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsProc, reinterpret_cast<LPARAM>(&ctx));

    if (m_monitors.empty()) {
        LOG_ERROR("WallpaperHost::Recover — Failed to recreate render windows on monitors");
        return E_FAIL;
    }

    LOG_INFO("WallpaperHost::Recover — Re-created %zu render windows parented to WorkerW (0x%p)", m_monitors.size(), m_workerW);
    return S_OK;
}

DWORD WallpaperHost::FindExplorerPID() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        LOG_ERROR("WallpaperHost::FindExplorerPID — CreateToolhelp32Snapshot failed");
        return 0;
    }

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;

    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }
    CloseHandle(snapshot);
    return pid;
}

HRESULT WallpaperHost::StartExplorerWatch(ExplorerCrashCallback callback, void* context) {
    if (!callback) return E_INVALIDARG;

    // Clean up previous watch handles
    if (m_waitHandle) {
        UnregisterWait(m_waitHandle);
        m_waitHandle = nullptr;
    }
    if (m_explorerProcess) {
        CloseHandle(m_explorerProcess);
        m_explorerProcess = nullptr;
    }

    DWORD explorerPid = FindExplorerPID();
    if (explorerPid == 0) {
        LOG_ERROR("WallpaperHost::StartExplorerWatch — Explorer.exe PID not found");
        return E_FAIL;
    }

    m_explorerProcess = OpenProcess(SYNCHRONIZE, FALSE, explorerPid);
    if (!m_explorerProcess) {
        DWORD err = GetLastError();
        LOG_ERROR("WallpaperHost::StartExplorerWatch — OpenProcess failed, error=%u", err);
        return HRESULT_FROM_WIN32(err);
    }

    BOOL ok = RegisterWaitForSingleObject(
        &m_waitHandle,
        m_explorerProcess,
        callback,
        context,
        INFINITE,
        WT_EXECUTEONLYONCE
    );

    if (!ok) {
        DWORD err = GetLastError();
        LOG_ERROR("WallpaperHost::StartExplorerWatch — RegisterWaitForSingleObject failed, error=%u", err);
        CloseHandle(m_explorerProcess);
        m_explorerProcess = nullptr;
        return HRESULT_FROM_WIN32(err);
    }

    LOG_INFO("WallpaperHost::StartExplorerWatch — Watching explorer.exe (PID=%u)", explorerPid);
    return S_OK;
}

void WallpaperHost::Shutdown() {
    if (m_waitHandle) {
        UnregisterWait(m_waitHandle);
        m_waitHandle = nullptr;
    }
    if (m_explorerProcess) {
        CloseHandle(m_explorerProcess);
        m_explorerProcess = nullptr;
    }

    for (auto& m : m_monitors) {
        if (m.renderWnd && IsWindow(m.renderWnd)) {
            DestroyWindow(m.renderWnd);
        }
    }
    m_monitors.clear();

    UnregisterClassW(kRenderClassName, GetModuleHandleW(nullptr));
    s_windowClassAtom = 0;
    m_workerW = nullptr;
    m_progman = nullptr;

    LOG_INFO("WallpaperHost::Shutdown — Render windows and watches cleared");
}

} // namespace lw
