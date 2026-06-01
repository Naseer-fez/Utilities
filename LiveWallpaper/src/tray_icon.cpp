#include "tray_icon.h"
#include "utils.h"
#include <shellapi.h>
#include <shobjidl.h> // For IFileOpenDialog

#define WM_TRAYICON (WM_USER + 1)
#define IDI_TRAY_ICON 1
#define IDM_CHANGE_VIDEO 1001
#define IDM_TOGGLE_PAUSE 1002
#define IDM_EXIT 1003

// Phase 7 Menu IDs
#define IDM_PLAYLIST_ADD 1004
#define IDM_PLAYLIST_CLEAR 1005
#define IDM_PLAYLIST_NEXT 1006
#define IDM_INTERVAL_MANUAL 1007
#define IDM_INTERVAL_1MIN 1008
#define IDM_INTERVAL_5MIN 1009
#define IDM_INTERVAL_15MIN 1010
#define IDM_INTERVAL_30MIN 1011
#define IDM_PLAYLIST_MANAGE 1012
#define IDM_FPS_UNLIMITED 1020
#define IDM_FPS_60 1021
#define IDM_FPS_30 1022
#define IDM_FPS_15 1023

TrayIcon::TrayIcon() {}

TrayIcon::~TrayIcon() {
    Shutdown();
}

bool TrayIcon::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    WNDCLASSEXW wcx = { 0 };
    wcx.cbSize = sizeof(wcx);
    wcx.lpfnWndProc = WndProc;
    wcx.hInstance = hInstance;
    wcx.lpszClassName = L"LiveWallpaperTrayClass";

    RegisterClassExW(&wcx);

    // Create a message-only window
    m_hWnd = CreateWindowExW(
        0,
        L"LiveWallpaperTrayClass",
        L"LiveWallpaperTray",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, hInstance, this
    );

    if (!m_hWnd) {
        LOG_ERROR("Failed to create message-only window for TrayIcon.");
        return false;
    }

    NOTIFYICONDATAW nid = { 0 };
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hWnd;
    nid.uID = IDI_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(NULL, IDI_APPLICATION); // Use default app icon for now
    wcscpy_s(nid.szTip, L"Live Wallpaper Engine");

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        LOG_ERROR("Shell_NotifyIcon NIM_ADD failed.");
        return false;
    }

    LOG_INFO("TrayIcon initialized successfully.");
    return true;
}

void TrayIcon::Shutdown() {
    if (m_hWnd) {
        NOTIFYICONDATAW nid = { 0 };
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hWnd;
        nid.uID = IDI_TRAY_ICON;
        Shell_NotifyIconW(NIM_DELETE, &nid);

        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }
}

void TrayIcon::SetChangeVideoCallback(std::function<void(const std::wstring&)> cb) {
    m_onChangeVideo = cb;
}

void TrayIcon::SetTogglePauseCallback(std::function<void(bool)> cb) {
    m_onTogglePause = cb;
}

void TrayIcon::SetExitCallback(std::function<void()> cb) {
    m_onExit = cb;
}

void TrayIcon::SetAddVideoCallback(std::function<void(const std::wstring&)> cb) {
    m_onAddVideo = cb;
}

void TrayIcon::SetClearPlaylistCallback(std::function<void()> cb) {
    m_onClearPlaylist = cb;
}

void TrayIcon::SetManagePlaylistCallback(std::function<void()> cb) {
    m_onManagePlaylist = cb;
}

void TrayIcon::SetNextVideoCallback(std::function<void()> cb) {
    m_onNextVideo = cb;
}

void TrayIcon::SetIntervalCallback(std::function<void(int)> cb) {
    m_onSetInterval = cb;
}

void TrayIcon::SetFPSLimitCallback(std::function<void(int)> cb) {
    m_onSetFPSLimit = cb;
}

void TrayIcon::UpdatePauseState(bool isPaused) {
    m_isPaused = isPaused;
}

void TrayIcon::UpdateRotationInterval(int minutes) {
    m_rotationIntervalMinutes = minutes;
}

void TrayIcon::UpdateHasPlaylist(bool hasPlaylist) {
    m_hasPlaylist = hasPlaylist;
}

void TrayIcon::UpdateFPSLimit(int fps) {
    m_fpsLimit = fps;
}

void TrayIcon::ShowContextMenu() {
    POINT pt;
    GetCursorPos(&pt);

    // Create cascading submenu for Playlist
    HMENU hPlaylistMenu = CreatePopupMenu();
    InsertMenuW(hPlaylistMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PLAYLIST_ADD, L"Add Video...");
    
    UINT nextFlags = MF_BYPOSITION | MF_STRING;
    if (!m_hasPlaylist) {
        nextFlags |= MF_GRAYED;
    }
    InsertMenuW(hPlaylistMenu, -1, nextFlags, IDM_PLAYLIST_NEXT, L"Next Video");
    InsertMenuW(hPlaylistMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PLAYLIST_MANAGE, L"Manage Playlist...");
    InsertMenuW(hPlaylistMenu, -1, MF_BYPOSITION | MF_STRING, IDM_PLAYLIST_CLEAR, L"Clear Playlist");
    InsertMenuW(hPlaylistMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);

    // Interval checks
    UINT checkManual = (m_rotationIntervalMinutes == 0) ? MF_CHECKED : MF_UNCHECKED;
    UINT check1 = (m_rotationIntervalMinutes == 1) ? MF_CHECKED : MF_UNCHECKED;
    UINT check5 = (m_rotationIntervalMinutes == 5) ? MF_CHECKED : MF_UNCHECKED;
    UINT check15 = (m_rotationIntervalMinutes == 15) ? MF_CHECKED : MF_UNCHECKED;
    UINT check30 = (m_rotationIntervalMinutes == 30) ? MF_CHECKED : MF_UNCHECKED;

    InsertMenuW(hPlaylistMenu, -1, MF_BYPOSITION | MF_STRING | checkManual, IDM_INTERVAL_MANUAL, L"Manual Only");
    InsertMenuW(hPlaylistMenu, -1, MF_BYPOSITION | MF_STRING | check1, IDM_INTERVAL_1MIN, L"Every 1 Minute");
    InsertMenuW(hPlaylistMenu, -1, MF_BYPOSITION | MF_STRING | check5, IDM_INTERVAL_5MIN, L"Every 5 Minutes");
    InsertMenuW(hPlaylistMenu, -1, MF_BYPOSITION | MF_STRING | check15, IDM_INTERVAL_15MIN, L"Every 15 Minutes");
    InsertMenuW(hPlaylistMenu, -1, MF_BYPOSITION | MF_STRING | check30, IDM_INTERVAL_30MIN, L"Every 30 Minutes");

    // Create cascading submenu for FPS Limit
    HMENU hFPSMenu = CreatePopupMenu();
    UINT checkFpsUnlimited = (m_fpsLimit == 0) ? MF_CHECKED : MF_UNCHECKED;
    UINT checkFps60 = (m_fpsLimit == 60) ? MF_CHECKED : MF_UNCHECKED;
    UINT checkFps30 = (m_fpsLimit == 30) ? MF_CHECKED : MF_UNCHECKED;
    UINT checkFps15 = (m_fpsLimit == 15) ? MF_CHECKED : MF_UNCHECKED;

    InsertMenuW(hFPSMenu, -1, MF_BYPOSITION | MF_STRING | checkFpsUnlimited, IDM_FPS_UNLIMITED, L"Unlimited (VSync)");
    InsertMenuW(hFPSMenu, -1, MF_BYPOSITION | MF_STRING | checkFps60, IDM_FPS_60, L"60 FPS");
    InsertMenuW(hFPSMenu, -1, MF_BYPOSITION | MF_STRING | checkFps30, IDM_FPS_30, L"30 FPS");
    InsertMenuW(hFPSMenu, -1, MF_BYPOSITION | MF_STRING | checkFps15, IDM_FPS_15, L"15 FPS");

    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hPlaylistMenu, L"Playlist");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hFPSMenu, L"FPS Limit");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_CHANGE_VIDEO, L"Change Video...");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_TOGGLE_PAUSE, m_isPaused ? L"Resume" : L"Pause");
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuW(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_EXIT, L"Exit");

    // Required for the menu to disappear when clicking outside
    SetForegroundWindow(m_hWnd);
    
    int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hWnd, NULL);
    DestroyMenu(hMenu); // This also cleans up hPlaylistMenu as it is nested

    switch (cmd) {
        case IDM_CHANGE_VIDEO:
            OpenVideoDialog();
            break;
        case IDM_TOGGLE_PAUSE:
            m_isPaused = !m_isPaused;
            if (m_onTogglePause) m_onTogglePause(m_isPaused);
            break;
        case IDM_EXIT:
            if (m_onExit) m_onExit();
            break;
        case IDM_PLAYLIST_ADD:
            OpenAddVideoDialog();
            break;
        case IDM_PLAYLIST_CLEAR:
            if (m_onClearPlaylist) m_onClearPlaylist();
            break;
        case IDM_PLAYLIST_MANAGE:
            if (m_onManagePlaylist) m_onManagePlaylist();
            break;
        case IDM_PLAYLIST_NEXT:
            if (m_onNextVideo) m_onNextVideo();
            break;
        case IDM_INTERVAL_MANUAL:
            if (m_onSetInterval) m_onSetInterval(0);
            break;
        case IDM_INTERVAL_1MIN:
            if (m_onSetInterval) m_onSetInterval(1);
            break;
        case IDM_INTERVAL_5MIN:
            if (m_onSetInterval) m_onSetInterval(5);
            break;
        case IDM_INTERVAL_15MIN:
            if (m_onSetInterval) m_onSetInterval(15);
            break;
        case IDM_INTERVAL_30MIN:
            if (m_onSetInterval) m_onSetInterval(30);
            break;
        case IDM_FPS_UNLIMITED:
            if (m_onSetFPSLimit) m_onSetFPSLimit(0);
            break;
        case IDM_FPS_60:
            if (m_onSetFPSLimit) m_onSetFPSLimit(60);
            break;
        case IDM_FPS_30:
            if (m_onSetFPSLimit) m_onSetFPSLimit(30);
            break;
        case IDM_FPS_15:
            if (m_onSetFPSLimit) m_onSetFPSLimit(15);
            break;
    }
}

void TrayIcon::OpenVideoDialog() {
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC rgSpec[] = {
            { L"Video Files", L"*.mp4;*.wmv;*.avi;*.mkv" },
            { L"All Files", L"*.*" }
        };
        pFileOpen->SetFileTypes(2, rgSpec);
        
        hr = pFileOpen->Show(NULL);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                PWSTR pszFilePath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr)) {
                    std::wstring selectedPath(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                    
                    if (m_onChangeVideo) {
                        m_onChangeVideo(selectedPath);
                    }
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
}

void TrayIcon::OpenAddVideoDialog() {
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC rgSpec[] = {
            { L"Video Files", L"*.mp4;*.wmv;*.avi;*.mkv" },
            { L"All Files", L"*.*" }
        };
        pFileOpen->SetFileTypes(2, rgSpec);
        
        hr = pFileOpen->Show(NULL);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                PWSTR pszFilePath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr)) {
                    std::wstring selectedPath(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                    
                    if (m_onAddVideo) {
                        m_onAddVideo(selectedPath);
                    }
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
}

LRESULT CALLBACK TrayIcon::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    TrayIcon* pThis = nullptr;

    if (message == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<TrayIcon*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
    } else {
        pThis = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    static const UINT wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (message == wmTaskbarCreated) {
        if (pThis) {
            pThis->RecreateTrayIcon();
        }
        return 0;
    }

    switch (message) {
        case WM_TRAYICON:
            if (LOWORD(lParam) == WM_RBUTTONUP) {
                if (pThis) {
                    pThis->ShowContextMenu();
                }
            }
            return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

void TrayIcon::RecreateTrayIcon() {
    if (!m_hWnd) return;

    NOTIFYICONDATAW nid = { 0 };
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_hWnd;
    nid.uID = IDI_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIconW(NULL, IDI_APPLICATION); // Use default app icon
    wcscpy_s(nid.szTip, L"Live Wallpaper Engine");

    if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        LOG_INFO("TrayIcon successfully recreated after Explorer restart.");
    } else {
        LOG_ERROR("Failed to recreate TrayIcon after Explorer restart.");
    }
}
