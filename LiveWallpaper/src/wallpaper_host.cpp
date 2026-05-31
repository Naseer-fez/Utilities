#include "wallpaper_host.h"
#include "utils.h"
#include <vector>

WallpaperHost::WallpaperHost() {
}

WallpaperHost::~WallpaperHost() {
    Shutdown();
}

bool WallpaperHost::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    m_isShuttingDown = false;
    LOG_INFO("Initializing Wallpaper Host...");

    if (!FindWorkerW()) {
        LOG_ERROR("Failed to find or create WorkerW desktop window.");
        return false;
    }

    if (!CreateHostWindow(hInstance)) {
        LOG_ERROR("Failed to create host window.");
        return false;
    }

    if (!InjectIntoDesktop()) {
        LOG_ERROR("Failed to inject host window into desktop.");
        return false;
    }

    m_lastUpdateTick = GetTickCount();
    LOG_INFO("Wallpaper Host successfully initialized and injected.");
    return true;
}

void WallpaperHost::Shutdown() {
    m_isShuttingDown = true;
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
    m_hWorkerW = nullptr;
    m_hShellDefView = nullptr;
    m_useLegacyWorkerW = true;
    LOG_INFO("Wallpaper Host shut down.");
}

bool WallpaperHost::FindWorkerW() {
    HWND progman = FindWindowW(L"Progman", NULL);
    if (!progman) {
        LOG_ERROR("Progman window not found.");
        return false;
    }
    LOG_INFO("Progman window found: %p. Sending 0x052C message...", progman);

    // Send 0x052C to Progman to spawn/ensure the WorkerW layer
    ULONG_PTR result = 0;
    SendMessageTimeoutW(
        progman,
        0x052C,
        0,
        0,
        SMTO_ABORTIFHUNG,
        1000,
        &result
    );

    LOG_INFO("SendMessageTimeoutW completed. Searching for desktop windows...");

    HWND shellDefView = NULL;
    HWND parentOfShell = NULL;
    HWND wallpaperWorkerW = NULL;

    // Search for SHELLDLL_DefView under Progman first (Win11 24H2+ check)
    shellDefView = FindWindowExW(progman, NULL, L"SHELLDLL_DefView", NULL);
    if (shellDefView) {
        parentOfShell = progman;
        LOG_INFO("Found SHELLDLL_DefView as child of Progman: %p", shellDefView);
    } else {
        // Fallback: search for SHELLDLL_DefView under top-level WorkerW windows (classic check)
        HWND workerW = FindWindowExW(NULL, NULL, L"WorkerW", NULL);
        while (workerW) {
            shellDefView = FindWindowExW(workerW, NULL, L"SHELLDLL_DefView", NULL);
            if (shellDefView) {
                parentOfShell = workerW;
                LOG_INFO("Found SHELLDLL_DefView under top-level WorkerW: %p (parent: %p)", shellDefView, parentOfShell);
                break;
            }
            workerW = FindWindowExW(NULL, workerW, L"WorkerW", NULL);
        }
    }

    if (!shellDefView) {
        LOG_ERROR("Could not find SHELLDLL_DefView window.");
        return false;
    }

    // Now, determine the parent and Z-ordering strategy based on the layout
    if (parentOfShell == progman) {
        // Windows 11 24H2+ / Raised desktop mode:
        // Parent our wallpaper window directly to Progman, and position it after SHELLDLL_DefView
        m_hWorkerW = progman;
        m_hShellDefView = shellDefView;
        m_useLegacyWorkerW = false;
        LOG_INFO("Using Progman parent strategy for Win11 24H2+ (Parent: %p, Insert After: %p)", m_hWorkerW, m_hShellDefView);
    } else {
        // Classic Windows 10/11:
        // Find the sibling WorkerW window that does NOT contain SHELLDLL_DefView
        HWND workerW = FindWindowExW(NULL, NULL, L"WorkerW", NULL);
        while (workerW) {
            if (workerW != parentOfShell) {
                // Confirm it has no SHELLDLL_DefView
                if (!FindWindowExW(workerW, NULL, L"SHELLDLL_DefView", NULL)) {
                    wallpaperWorkerW = workerW;
                    break;
                }
            }
            workerW = FindWindowExW(NULL, workerW, L"WorkerW", NULL);
        }

        if (wallpaperWorkerW) {
            m_hWorkerW = wallpaperWorkerW;
            m_hShellDefView = NULL;
            m_useLegacyWorkerW = true;
            LOG_INFO("Using legacy WorkerW parent strategy (Parent: %p)", m_hWorkerW);
        } else {
            LOG_WARN("Legacy WorkerW sibling not found. Falling back to Progman directly.");
            m_hWorkerW = progman;
            m_hShellDefView = NULL;
            m_useLegacyWorkerW = false;
        }
    }

    return m_hWorkerW != nullptr;
}

bool WallpaperHost::CreateHostWindow(HINSTANCE hInstance) {
    WNDCLASSEXW wcx = { 0 };
    wcx.cbSize = sizeof(wcx);
    wcx.style = CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc = WndProc;
    wcx.hInstance = hInstance;
    wcx.lpszClassName = L"LiveWallpaperHostClass";
    wcx.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wcx.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    // Register class (ignore failure if already registered)
    RegisterClassExW(&wcx);

    // Get virtual screen bounds (supports multi-monitor setups)
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // Create as child window, parented to WorkerW
    m_hWnd = CreateWindowExW(
        WS_EX_NOACTIVATE,
        L"LiveWallpaperHostClass",
        L"LiveWallpaperHost",
        WS_CHILD | WS_VISIBLE,
        x, y, cx, cy,
        m_hWorkerW,
        NULL,
        hInstance,
        this
    );

    if (!m_hWnd) {
        LOG_ERROR("CreateWindowExW failed for LiveWallpaperHost. Error: 0x%08X", GetLastError());
        return false;
    }

    return true;
}

bool WallpaperHost::InjectIntoDesktop() {
    if (!m_hWnd || !m_hWorkerW) return false;

    // Double check parent is indeed WorkerW
    HWND currentParent = GetParent(m_hWnd);
    if (currentParent != m_hWorkerW) {
        SetParent(m_hWnd, m_hWorkerW);
    }

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HWND hWndInsertAfter = HWND_BOTTOM;
    if (!m_useLegacyWorkerW && m_hShellDefView) {
        hWndInsertAfter = m_hShellDefView;
    }

    SetWindowPos(
        m_hWnd,
        hWndInsertAfter,
        x,
        y,
        cx,
        cy,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );

    return true;
}

void WallpaperHost::Update() {
    DWORD currentTick = GetTickCount();
    if (currentTick - m_lastUpdateTick < 2000) {
        return; // Check every 2 seconds to avoid overhead
    }
    m_lastUpdateTick = currentTick;

    // Check if explorer crashed/restarted
    HWND progman = FindWindowW(L"Progman", NULL);
    bool needsRecovery = false;

    if (!progman || !IsWindow(m_hWnd) || !IsWindow(m_hWorkerW)) {
        needsRecovery = true;
    } else {
        // Verify we are still children of a valid WorkerW/Progman
        HWND parent = GetParent(m_hWnd);
        if (!parent || parent != m_hWorkerW) {
            needsRecovery = true;
        }
    }

    if (needsRecovery) {
        LOG_WARN("Explorer restart or window invalidation detected! Triggering recovery...");
        m_isRecovering = true;
        Shutdown();
        
        // Re-initialize
        if (Initialize(m_hInstance)) {
            LOG_INFO("Successfully recovered and re-injected wallpaper host.");
        } else {
            LOG_ERROR("Failed to recover wallpaper host.");
        }
        m_isRecovering = false;
    } else {
        // Self-healing: On Windows 11 24H2+, periodically enforce Z-order relative to SHELLDLL_DefView
        if (!m_useLegacyWorkerW && m_hShellDefView) {
            SetWindowPos(
                m_hWnd,
                m_hShellDefView,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
            );
        }
    }
}

LRESULT CALLBACK WallpaperHost::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WallpaperHost* pThis = nullptr;
    if (message == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<WallpaperHost*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<WallpaperHost*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    switch (message) {
        case WM_DESTROY:
            if (pThis && (pThis->m_isRecovering || pThis->m_isShuttingDown)) {
                LOG_INFO("WallpaperHost window destroyed due to recovery or shutdown. Skipping PostQuitMessage.");
            } else {
                LOG_WARN("WallpaperHost window destroyed unexpectedly (e.g. Explorer crash)! Skipping PostQuitMessage to allow recovery.");
            }
            return 0;
            
        case WM_ERASEBKGND:
            return 1; // Handled to prevent flicker
            
        case WM_DISPLAYCHANGE: {
            LOG_INFO("Display change detected. Resizing wallpaper host window.");
            // Recalculate dimensions for virtual desktop
            int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
            int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
            int cx = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            int cy = GetSystemMetrics(SM_CYVIRTUALSCREEN);

            HWND hWndInsertAfter = HWND_BOTTOM;
            if (pThis && !pThis->m_useLegacyWorkerW && pThis->m_hShellDefView) {
                hWndInsertAfter = pThis->m_hShellDefView;
            }
            SetWindowPos(hWnd, hWndInsertAfter, x, y, cx, cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            return 0;
        }
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}
