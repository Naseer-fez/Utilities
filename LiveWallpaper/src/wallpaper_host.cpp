#include "wallpaper_host.h"
#include "utils.h"

// We now use FindWindowEx instead of EnumWindows to avoid hangs on other processes' windows.

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
    LOG_INFO("Wallpaper Host shut down.");
}

bool WallpaperHost::FindWorkerW() {
    HWND progman = FindWindowW(L"Progman", NULL);
    if (!progman) {
        LOG_ERROR("Progman window not found.");
        return false;
    }

    // Send 0x052C to Progman to spawn WorkerW behind icons
    ULONG_PTR result = 0;
    SendMessageTimeoutW(
        progman,
        0x052C,
        0,
        0,
        SMTO_NORMAL,
        1000,
        &result
    );

    // Wait and retry up to 10 times to find the WorkerW window immediately behind the shell desktop icons window
    HWND wallpaperWorkerW = NULL;
    for (int attempt = 0; attempt < 10; ++attempt) {
        HWND shellDLLWindow = NULL;
        HWND workerW = NULL;
        
        // Find the top-level window holding SHELLDLL_DefView (desktop icons container)
        while ((workerW = FindWindowExW(NULL, workerW, L"WorkerW", NULL)) != NULL) {
            if (FindWindowExW(workerW, NULL, L"SHELLDLL_DefView", NULL) != NULL) {
                shellDLLWindow = workerW;
                break;
            }
        }
        
        if (!shellDLLWindow) {
            HWND progmanInstance = FindWindowW(L"Progman", NULL);
            if (progmanInstance && FindWindowExW(progmanInstance, NULL, L"SHELLDLL_DefView", NULL) != NULL) {
                shellDLLWindow = progmanInstance;
            }
        }
        
        if (shellDLLWindow) {
            // Find the WorkerW sibling immediately behind the shell window in Z-order
            wallpaperWorkerW = FindWindowExW(NULL, shellDLLWindow, L"WorkerW", NULL);
            if (wallpaperWorkerW) {
                m_hWorkerW = wallpaperWorkerW;
                LOG_INFO("WorkerW background sibling window found after %d attempts.", attempt + 1);
                break;
            }
        }
        Sleep(100);
    }

    if (!m_hWorkerW) {
        LOG_WARN("WorkerW sibling not found. Falling back to Progman directly.");
        m_hWorkerW = progman;
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

    m_hWnd = CreateWindowExW(
        WS_EX_NOACTIVATE,
        L"LiveWallpaperHostClass",
        L"LiveWallpaperHost",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
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

    SetWindowPos(
        m_hWnd,
        HWND_BOTTOM,
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
            SetWindowPos(hWnd, HWND_BOTTOM, x, y, cx, cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            return 0;
        }
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}
