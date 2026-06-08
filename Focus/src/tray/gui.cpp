#include "gui.h"
#include "../resource.h"
#include "../common/utils.h"
#include <commctrl.h>
#include <commdlg.h>

#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>

#define WM_STATUS_UPDATE (WM_USER + 2)

struct StatusUpdateData {
    bool isStrictActive;
    int timeRemainingSeconds;
    wchar_t profileName[64];
};

// Global pointer for static dialog proc callback
static TrayUI* s_pTrayUI = nullptr;
static SmtpConfig s_localSmtp; // Backup for writing config
static bool s_autoRequestUnlock = false;

TrayUI::TrayUI(HINSTANCE hInstance)
    : m_hInstance(hInstance), m_hwnd(NULL), m_hDlgControlPanel(NULL), m_isStrictActive(false),
      m_timeRemainingSeconds(0), m_activeProfileName(L""), m_hPollThread(NULL), m_pollThreadRunning(false) {
    
    s_pTrayUI = this;
    
    // Create premium dark theme brushes
    m_hDarkBgBrush = CreateSolidBrush(RGB(20, 20, 23));
    m_hAccentBrush = CreateSolidBrush(RGB(0, 120, 215));
    m_hEditBgBrush = CreateSolidBrush(RGB(40, 40, 45));
    
    // Create clean modern fonts
    m_hFontTitle = CreateFontW(
        22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    m_hFontText = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    // Initial load of SMTP backup
    std::wstring configPath = getAppDirectory() + L"\\config.json";
    loadSmtpConfig(configPath, s_localSmtp);
}

TrayUI::~TrayUI() {
    m_pollThreadRunning = false;
    if (m_hPollThread) {
        WaitForSingleObject(m_hPollThread, 2000);
        CloseHandle(m_hPollThread);
        m_hPollThread = NULL;
    }

    removeTrayIcon();
    
    if (m_hDarkBgBrush) DeleteObject(m_hDarkBgBrush);
    if (m_hAccentBrush) DeleteObject(m_hAccentBrush);
    if (m_hEditBgBrush) DeleteObject(m_hEditBgBrush);
    if (m_hFontTitle) DeleteObject(m_hFontTitle);
    if (m_hFontText) DeleteObject(m_hFontText);
    if (m_nid.hIcon) DestroyIcon(m_nid.hIcon);
    
    if (m_hwnd) DestroyWindow(m_hwnd);
}

bool TrayUI::initialize() {
    // Load profiles from config
    refreshProfiles();
    
    if (!createHiddenWindow()) {
        logMessage(L"TrayUI: Failed to create hidden window.");
        return false;
    }

    setupTrayIcon();

    // Query status immediately to sync with existing sessions
    queryStatus();

    // Start background thread to poll Status asynchronously instead of WM_TIMER polling on UI thread
    m_pollThreadRunning = true;
    m_hPollThread = CreateThread(NULL, 0, pollThreadFunc, this, 0, NULL);
    
    return true;
}

void TrayUI::run() {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool TrayUI::createHiddenWindow() {
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        if (s_pTrayUI) {
            return s_pTrayUI->handleMessage(hwnd, uMsg, wParam, lParam);
        }
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    };
    wc.hInstance = m_hInstance;
    wc.lpszClassName = L"FocusTrayUIWindow";
    
    RegisterClassW(&wc);
    
    m_hwnd = CreateWindowExW(
        0, L"FocusTrayUIWindow", L"FocusTrayUIWindow",
        0, 0, 0, 0, 0, HWND_MESSAGE, NULL, m_hInstance, NULL
    );
    
    return (m_hwnd != NULL);
}

static HICON createBlackIcon() {
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    HDC hdc = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmColor = CreateCompatibleBitmap(hdc, cx, cy);
    HBITMAP hbmMask = CreateBitmap(cx, cy, 1, 1, NULL); // all 0 (black mask)
    
    HGDIOBJ hOldSel = SelectObject(hdcMem, hbmColor);
    RECT rc = { 0, 0, cx, cy };
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdcMem, &rc, hBrush);
    DeleteObject(hBrush);
    
    SelectObject(hdcMem, hOldSel);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdc);
    
    ICONINFO ii = {0};
    ii.fIcon = TRUE;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;
    
    HICON hIcon = CreateIconIndirect(&ii);
    
    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    
    return hIcon;
}

bool TrayUI::notifyDaemonReloadConfig() {
    IpcMessage sendMsg;
    ZeroMemory(&sendMsg, sizeof(IpcMessage));
    sendMsg.type = IpcCommandType::ReloadConfig;

    IpcMessage reply;
    ZeroMemory(&reply, sizeof(IpcMessage));
    DWORD bytesRead = 0;
    
    BOOL success = CallNamedPipeW(
        FOCUS_PIPE_NAME,
        (LPVOID)&sendMsg,
        sizeof(IpcMessage),
        &reply,
        sizeof(IpcMessage),
        &bytesRead,
        2000
    );
    
    return (success && bytesRead == sizeof(IpcMessage) && reply.success);
}

void TrayUI::setupTrayIcon() {
    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = ID_TRAY_ICON;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    m_nid.uCallbackMessage = WM_TRAYICON;
    
    // Load our custom compiled app icon using LoadImageW for maximum compatibility
    m_nid.hIcon = (HICON)LoadImageW(
        m_hInstance,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_SHARED
    );
    if (!m_nid.hIcon) {
        m_nid.hIcon = createBlackIcon();
    }
    
    wcscpy_s(m_nid.szTip, L"Focus Mode Engine - Idle");
    
    Shell_NotifyIconW(NIM_ADD, &m_nid);
}

void TrayUI::removeTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &m_nid);
}

void TrayUI::refreshProfiles() {
    std::wstring configPath = getAppDirectory() + L"\\config.json";
    m_profiles = loadProfiles(configPath);
    loadSmtpConfig(configPath, s_localSmtp);
}

bool TrayUI::queryStatus() {
    IpcMessage sendMsg;
    ZeroMemory(&sendMsg, sizeof(IpcMessage));
    sendMsg.type = IpcCommandType::GetStatus;

    IpcMessage reply;
    ZeroMemory(&reply, sizeof(IpcMessage));

    DWORD bytesRead = 0;
    BOOL success = CallNamedPipeW(
        FOCUS_PIPE_NAME,
        (LPVOID)&sendMsg,
        sizeof(IpcMessage),
        &reply,
        sizeof(IpcMessage),
        &bytesRead,
        2000
    );

    if (success && bytesRead == sizeof(IpcMessage)) {
        StatusUpdateData* pData = new StatusUpdateData;
        pData->isStrictActive = reply.isStrictActive;
        pData->timeRemainingSeconds = reply.timeRemainingSeconds;
        wcscpy_s(pData->profileName, reply.profileName);
        SendMessageW(m_hwnd, WM_STATUS_UPDATE, 0, (LPARAM)pData);
        return true;
    }
    return false;
}

void TrayUI::queryStatusAsync() {
    IpcMessage sendMsg;
    ZeroMemory(&sendMsg, sizeof(IpcMessage));
    sendMsg.type = IpcCommandType::GetStatus;

    IpcMessage reply;
    ZeroMemory(&reply, sizeof(IpcMessage));

    DWORD bytesRead = 0;
    BOOL success = CallNamedPipeW(
        FOCUS_PIPE_NAME,
        (LPVOID)&sendMsg,
        sizeof(IpcMessage),
        &reply,
        sizeof(IpcMessage),
        &bytesRead,
        1000
    );

    if (success && bytesRead == sizeof(IpcMessage)) {
        StatusUpdateData* pData = new StatusUpdateData;
        pData->isStrictActive = reply.isStrictActive;
        pData->timeRemainingSeconds = reply.timeRemainingSeconds;
        wcscpy_s(pData->profileName, reply.profileName);
        PostMessageW(m_hwnd, WM_STATUS_UPDATE, 0, (LPARAM)pData);
    }
}

DWORD WINAPI TrayUI::pollThreadFunc(LPVOID lpParam) {
    TrayUI* pUI = static_cast<TrayUI*>(lpParam);
    while (pUI->isPollThreadRunning()) {
        pUI->queryStatusAsync();
        Sleep(1000);
    }
    return 0;
}

bool TrayUI::startSession(const std::wstring& profileName) {
    IpcMessage sendMsg;
    ZeroMemory(&sendMsg, sizeof(IpcMessage));
    sendMsg.type = IpcCommandType::StartSession;
    wcscpy_s(sendMsg.profileName, profileName.c_str());

    IpcMessage reply;
    ZeroMemory(&reply, sizeof(IpcMessage));
    DWORD bytesRead = 0;
    
    BOOL success = CallNamedPipeW(
        FOCUS_PIPE_NAME,
        (LPVOID)&sendMsg,
        sizeof(IpcMessage),
        &reply,
        sizeof(IpcMessage),
        &bytesRead,
        2000
    );
    
    if (success && bytesRead == sizeof(IpcMessage) && reply.success) {
        queryStatus();
        return true;
    }
    return false;
}

bool TrayUI::submitUnlockCode(const std::wstring& code) {
    IpcMessage sendMsg;
    ZeroMemory(&sendMsg, sizeof(IpcMessage));
    sendMsg.type = IpcCommandType::SubmitUnlockCode;
    wcscpy_s(sendMsg.unlockCode, code.c_str());

    IpcMessage reply;
    ZeroMemory(&reply, sizeof(IpcMessage));
    DWORD bytesRead = 0;
    
    BOOL success = CallNamedPipeW(
        FOCUS_PIPE_NAME,
        (LPVOID)&sendMsg,
        sizeof(IpcMessage),
        &reply,
        sizeof(IpcMessage),
        &bytesRead,
        2000
    );
    
    if (success && bytesRead == sizeof(IpcMessage) && reply.success) {
        queryStatus();
        return true;
    }
    return false;
}

bool TrayUI::requestUnlock() {
    IpcMessage sendMsg;
    ZeroMemory(&sendMsg, sizeof(IpcMessage));
    sendMsg.type = IpcCommandType::RequestUnlock;

    IpcMessage reply;
    ZeroMemory(&reply, sizeof(IpcMessage));
    DWORD bytesRead = 0;
    
    BOOL success = CallNamedPipeW(
        FOCUS_PIPE_NAME,
        (LPVOID)&sendMsg,
        sizeof(IpcMessage),
        &reply,
        sizeof(IpcMessage),
        &bytesRead,
        2000
    );
    
    if (success && bytesRead == sizeof(IpcMessage) && reply.success) {
        m_nid.uFlags = NIF_INFO;
        wcscpy_s(m_nid.szInfoTitle, L"Unlock Code Dispatched");
        std::wstring msg = L"A new one-time unlock code has been sent to your configured email address. Enter the code in the Control Panel to unlock.";
        wcscpy_s(m_nid.szInfo, msg.c_str());
        m_nid.dwInfoFlags = NIIF_INFO;
        Shell_NotifyIconW(NIM_MODIFY, &m_nid);
        return true;
    }
    return false;
}

void TrayUI::showContextMenu() {
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    if (!m_isStrictActive) {
        // 1. Open Control Panel Option (Sleek Whitelist GUI)
        AppendMenuW(hMenu, MF_STRING, ID_MENU_CONTROL_PANEL, L"Open Focus Control Panel");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        
        // 2. Profile Start submenu
        HMENU hSubMenu = CreatePopupMenu();
        refreshProfiles(); // Refresh configs in case they changed
        
        if (m_profiles.empty()) {
            AppendMenuW(hSubMenu, MF_STRING | MF_GRAYED, 0, L"No Profiles Found");
        } else {
            for (size_t i = 0; i < m_profiles.size(); ++i) {
                AppendMenuW(hSubMenu, MF_STRING, ID_MENU_PROFILE_START + i, m_profiles[i].name.c_str());
            }
        }
        
        AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"Quick Start Profile");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, ID_MENU_EXIT, L"Exit Focus Engine");
    } else {
        // Strict Focus menu
        int mins = m_timeRemainingSeconds / 60;
        int secs = m_timeRemainingSeconds % 60;
        std::wstringstream ss;
        ss << L"Session Active: " << m_activeProfileName << L" ("
           << std::setw(2) << std::setfill(L'0') << mins << L":"
           << std::setw(2) << std::setfill(L'0') << secs << L")";
        
        AppendMenuW(hMenu, MF_STRING | MF_GRAYED, ID_MENU_STATUS, ss.str().c_str());
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, ID_MENU_CONTROL_PANEL, L"Open Focus Control Panel");
        AppendMenuW(hMenu, MF_STRING, ID_MENU_UNLOCK, L"Request Session Unlock");
        AppendMenuW(hMenu, MF_STRING, ID_MENU_ENTER_CODE, L"Enter Unlock Code");
    }

    SetForegroundWindow(m_hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, m_hwnd, NULL);
    DestroyMenu(hMenu);
}

// Custom Dialog template in raw memory (saves having to bundle resource files in g++)
// createUnlockDialogTemplate and UnlockDlgProc have been unified into ControlPanelDlgProc inline fields to prevent spam and code invalidation.

// Memory allocator for the Control Panel Dialog Template
static LPDLGTEMPLATE createControlPanelDialogTemplate() {
    LPDLGTEMPLATE lpd = (LPDLGTEMPLATE)LocalAlloc(LPTR, 1024);
    if (!lpd) return nullptr;
    
    lpd->style = DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    lpd->dwExtendedStyle = 0;
    lpd->cdit = 0; // programmatically populated in WM_INITDIALOG
    lpd->x  = 100; lpd->y  = 100;
    lpd->cx = 360; lpd->cy = 250;

    LPWORD lpw = (LPWORD)(lpd + 1);
    *lpw++ = 0;             // No menu
    *lpw++ = 0;             // Default class
    
    LPCWSTR title = L"Focus Whitelist Control Panel";
    while (*title) {
        *lpw++ = (WORD)*title++;
    }
    *lpw++ = 0;
    
    *lpw++ = 9;
    LPCWSTR fontName = L"Segoe UI";
    while (*fontName) {
        *lpw++ = (WORD)*fontName++;
    }
    *lpw++ = 0;
    
    return lpd;
}

struct InputDlgParams {
    std::wstring title;
    std::wstring prompt;
    std::wstring value;
};

INT_PTR CALLBACK TrayUI::InputDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static InputDlgParams* pParams = nullptr;
    static HWND hEdit = nullptr;

    switch (message) {
        case WM_INITDIALOG: {
            pParams = (InputDlgParams*)lParam;
            
            SetWindowTextW(hDlg, pParams->title.c_str());

            HWND hStaticPrompt = CreateWindowExW(
                0, L"STATIC", pParams->prompt.c_str(),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                15, 12, 230, 16, hDlg, NULL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hStaticPrompt, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            hEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", pParams->value.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                15, 32, 230, 24, hDlg, NULL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hEdit, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);
            SetFocus(hEdit);
            SendMessageW(hEdit, EM_SETSEL, 0, -1);

            HWND hBtnOk = CreateWindowExW(
                0, L"BUTTON", L"OK",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                60, 68, 65, 24, hDlg, (HMENU)IDOK, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnOk, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            HWND hBtnCancel = CreateWindowExW(
                0, L"BUTTON", L"Cancel",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                135, 68, 65, 24, hDlg, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            RECT rect;
            GetWindowRect(GetDesktopWindow(), &rect);
            int width = 270;
            int height = 140;
            SetWindowPos(
                hDlg, HWND_TOPMOST,
                (rect.right - width) / 2,
                (rect.bottom - height) / 2,
                width, height, SWP_SHOWWINDOW
            );

            return FALSE;
        }

        case WM_CTLCOLORDLG: {
            return (INT_PTR)s_pTrayUI->m_hDarkBgBrush;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(230, 230, 235));
            SetBkColor(hdc, RGB(20, 20, 23));
            return (INT_PTR)s_pTrayUI->m_hDarkBgBrush;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(40, 40, 45));
            return (INT_PTR)s_pTrayUI->m_hEditBgBrush;
        }

        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(240, 240, 240));
            SetBkColor(hdc, RGB(20, 20, 23));
            return (INT_PTR)s_pTrayUI->m_hDarkBgBrush;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == IDOK) {
                wchar_t buf[256];
                GetWindowTextW(hEdit, buf, 256);
                if (pParams) {
                    pParams->value = buf;
                }
                EndDialog(hDlg, IDOK);
                return TRUE;
            } else if (wmId == IDCANCEL) {
                EndDialog(hDlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

static LPDLGTEMPLATE createInputDialogTemplate() {
    LPDLGTEMPLATE lpd = (LPDLGTEMPLATE)LocalAlloc(LPTR, 1024);
    if (!lpd) return nullptr;
    
    lpd->style = DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    lpd->dwExtendedStyle = 0;
    lpd->cdit = 0;
    lpd->x  = 100; lpd->y  = 100;
    lpd->cx = 160; lpd->cy = 80;
    
    LPWORD lpw = (LPWORD)(lpd + 1);
    *lpw++ = 0;             // No menu
    *lpw++ = 0;             // Default class
    
    LPCWSTR title = L"Input Dialog";
    while (*title) {
        *lpw++ = (WORD)*title++;
    }
    *lpw++ = 0;
    
    *lpw++ = 9;
    LPCWSTR fontName = L"Segoe UI";
    while (*fontName) {
        *lpw++ = (WORD)*fontName++;
    }
    *lpw++ = 0;
    
    return lpd;
}

bool TrayUI::showInputDialog(HWND parent, const std::wstring& title, const std::wstring& prompt, std::wstring& outValue) {
    InputDlgParams params;
    params.title = title;
    params.prompt = prompt;
    params.value = outValue;

    LPDLGTEMPLATE lpd = createInputDialogTemplate();
    if (!lpd) return false;

    INT_PTR res = DialogBoxIndirectParamW(
        GetModuleHandle(NULL),
        lpd,
        parent,
        InputDlgProc,
        (LPARAM)&params
    );

    LocalFree(lpd);

    if (res == IDOK) {
        outValue = params.value;
        return true;
    }
    return false;
}

static void updateListAndLabel(HWND hDlg, HWND hStaticList, HWND hListAllowed, const Profile& profile) {
    SendMessageW(hListAllowed, LB_RESETCONTENT, 0, 0);
    if (profile.blockAllExceptAllowed) {
        SetWindowTextW(hStaticList, L"Allowed Applications Whitelist:");
        for (const auto& app : profile.allowedApps) {
            SendMessageW(hListAllowed, LB_ADDSTRING, 0, (LPARAM)app.c_str());
        }
    } else {
        SetWindowTextW(hStaticList, L"Blocked Applications Blacklist:");
        for (const auto& app : profile.appsToClose) {
            SendMessageW(hListAllowed, LB_ADDSTRING, 0, (LPARAM)app.c_str());
        }
    }
}

// Dialog Procedure for Custom Whitelist Control Panel GUI
INT_PTR CALLBACK TrayUI::ControlPanelDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static HWND hStaticProfile = NULL;
    static HWND hComboProfile = NULL;
    static HWND hBtnNew = NULL;
    static HWND hBtnDelete = NULL;
    static HWND hCheckWhitelist = NULL;
    
    static HWND hStaticDuration = NULL;
    static HWND hEditDuration = NULL;
    static HWND hStaticMins = NULL;
    
    static HWND hStaticVolume = NULL;
    static HWND hEditVolume = NULL;
    static HWND hStaticPercent = NULL;
    
    static HWND hStaticWallpaper = NULL;
    static HWND hEditWallpaper = NULL;
    static HWND hBtnWallpaper = NULL;

    static HWND hStaticList = NULL;
    static HWND hListAllowed = NULL;
    static HWND hEditAdd = NULL;
    static HWND hBtnAdd = NULL;
    static HWND hBtnRemove = NULL;
    static HWND hBtnStart = NULL;
    static HWND hBtnStop = NULL;
    static HWND hStaticActiveStatus = NULL;
    static HWND hBtnRequestCode = NULL;
    static HWND hStaticEnterCode = NULL;
    static HWND hEditUnlockCode = NULL;
    static HWND hBtnUnlock = NULL;
    
    static int s_currentProfileIndex = 0;
    static bool s_isUpdatingFields = false;
    static int s_emailCooldownSeconds = 0;

    switch (message) {
        case WM_INITDIALOG: {
            s_pTrayUI->m_hDlgControlPanel = hDlg;
            s_emailCooldownSeconds = 0;
            s_pTrayUI->refreshProfiles(); // reload config.json
            s_currentProfileIndex = 0;
            s_isUpdatingFields = true;

            // --- LEFT COLUMN ---
            // 1. Profile Select Label
            hStaticProfile = CreateWindowExW(
                0, L"STATIC", L"Configure Profile:",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                15, 10, 160, 18, hDlg, NULL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hStaticProfile, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // 2. ComboBox for Profiles
            hComboProfile = CreateWindowExW(
                0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                15, 28, 160, 100, hDlg, (HMENU)IDC_PROFILE_COMBO, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hComboProfile, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);
            for (size_t i = 0; i < s_pTrayUI->m_profiles.size(); ++i) {
                SendMessageW(hComboProfile, CB_ADDSTRING, 0, (LPARAM)s_pTrayUI->m_profiles[i].name.c_str());
            }
            SendMessageW(hComboProfile, CB_SETCURSEL, s_currentProfileIndex, 0);

            // 3. New Profile Button
            hBtnNew = CreateWindowExW(
                0, L"BUTTON", L"New",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                180, 27, 35, 24, hDlg, (HMENU)IDC_NEW_PROFILE_BTN, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnNew, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // 4. Delete Profile Button
            hBtnDelete = CreateWindowExW(
                0, L"BUTTON", L"Delete",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                220, 27, 40, 24, hDlg, (HMENU)IDC_DEL_PROFILE_BTN, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnDelete, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // Get selected profile parameters
            Profile profile;
            if (!s_pTrayUI->m_profiles.empty()) {
                profile = s_pTrayUI->m_profiles[s_currentProfileIndex];
            }

            // 5. Whitelist Checkbox
            hCheckWhitelist = CreateWindowExW(
                0, L"BUTTON", L"Block ALL apps except allowed whitelist",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                15, 60, 245, 20, hDlg, (HMENU)IDC_WHITELIST_CHECK, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hCheckWhitelist, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);
            SendMessageW(hCheckWhitelist, BM_SETCHECK, profile.blockAllExceptAllowed ? BST_CHECKED : BST_UNCHECKED, 0);

            // 6. Duration Controls
            hStaticDuration = CreateWindowExW(
                0, L"STATIC", L"Session Duration:",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                15, 90, 100, 18, hDlg, NULL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hStaticDuration, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            hEditDuration = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(profile.durationMinutes).c_str(),
                WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                120, 87, 50, 22, hDlg, (HMENU)IDC_DURATION_EDIT, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hEditDuration, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            hStaticMins = CreateWindowExW(
                0, L"STATIC", L"mins",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                175, 90, 40, 18, hDlg, NULL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hStaticMins, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // 7. Volume Controls
            hStaticVolume = CreateWindowExW(
                0, L"STATIC", L"System Volume:",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                15, 120, 100, 18, hDlg, NULL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hStaticVolume, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            hEditVolume = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(profile.volume).c_str(),
                WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER | WS_TABSTOP,
                120, 117, 50, 22, hDlg, (HMENU)IDC_VOLUME_EDIT, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hEditVolume, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            hStaticPercent = CreateWindowExW(
                0, L"STATIC", L"%",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                175, 120, 40, 18, hDlg, NULL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hStaticPercent, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // 8. Wallpaper Controls
            hStaticWallpaper = CreateWindowExW(
                0, L"STATIC", L"Session Wallpaper Background Image:",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                15, 150, 245, 18, hDlg, NULL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hStaticWallpaper, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            hEditWallpaper = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", profile.wallpaperPath.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                15, 168, 175, 22, hDlg, (HMENU)IDC_WALLPAPER_EDIT, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hEditWallpaper, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            hBtnWallpaper = CreateWindowExW(
                0, L"BUTTON", L"Browse...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                195, 167, 65, 24, hDlg, (HMENU)IDC_WALLPAPER_BTN, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnWallpaper, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // --- RIGHT COLUMN ---
            // 9. Allowed Apps Label
            hStaticList = CreateWindowExW(
                0, L"STATIC", L"Allowed Applications Whitelist:",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                295, 10, 240, 18, hDlg, NULL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hStaticList, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // 10. ListBox for Allowed Apps
            hListAllowed = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP,
                295, 28, 240, 110, hDlg, (HMENU)IDC_WHITELIST_LIST, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hListAllowed, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);
            updateListAndLabel(hDlg, hStaticList, hListAllowed, profile);

            // 11. Add App Edit Control
            hEditAdd = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                295, 146, 155, 24, hDlg, (HMENU)IDC_ADD_EDIT, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hEditAdd, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // 12. Add App Button
            hBtnAdd = CreateWindowExW(
                0, L"BUTTON", L"Add App",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                455, 145, 80, 26, hDlg, (HMENU)IDC_ADD_BTN, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnAdd, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // 13. Remove App Button
            hBtnRemove = CreateWindowExW(
                0, L"BUTTON", L"Remove Selected App",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                295, 178, 240, 26, hDlg, (HMENU)IDC_REMOVE_BTN, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnRemove, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // --- BOTTOM SECTION ---
            // 14. Start Focus Button
            hBtnStart = CreateWindowExW(
                0, L"BUTTON", L"Start Focus Mode Session",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                15, 230, 520, 30, hDlg, (HMENU)IDC_START_BTN, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnStart, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);
            // Set initial default button to Start (session not active on first open)
            SendMessageW(hDlg, DM_SETDEFID, IDC_START_BTN, 0);

            // 15. Stop Focus Button
            hBtnStop = CreateWindowExW(
                0, L"BUTTON", L"Stop / Unlock Current Session",
                WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
                15, 265, 520, 30, hDlg, (HMENU)IDC_STOP_BTN, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnStop, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);
            ShowWindow(hBtnStop, SW_HIDE); // Always hide the old Stop button, we use inline fields instead

            // New inline unlock controls (created hidden, shown when active)
            hStaticActiveStatus = CreateWindowExW(
                0, L"STATIC", L"ACTIVE FOCUS SESSION: None",
                WS_CHILD | SS_CENTER,
                15, 210, 520, 18, hDlg, NULL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hStaticActiveStatus, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontTitle, TRUE);

            hBtnRequestCode = CreateWindowExW(
                0, L"BUTTON", L"Request Code via Email",
                WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
                15, 230, 200, 26, hDlg, (HMENU)3015, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnRequestCode, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            hStaticEnterCode = CreateWindowExW(
                0, L"STATIC", L"Enter Code:",
                WS_CHILD | SS_LEFT,
                230, 234, 70, 18, hDlg, NULL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hStaticEnterCode, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            hEditUnlockCode = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | ES_CENTER | ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP,
                310, 231, 110, 24, hDlg, (HMENU)3017, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hEditUnlockCode, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            hBtnUnlock = CreateWindowExW(
                0, L"BUTTON", L"Unlock Session",
                WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
                430, 230, 105, 26, hDlg, (HMENU)3016, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnUnlock, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // Timer to update active session status dynamically
            SetTimer(hDlg, 2, 1000, NULL);

            if (s_autoRequestUnlock) {
                s_autoRequestUnlock = false;
                s_pTrayUI->requestUnlock();
                s_emailCooldownSeconds = 60;
                EnableWindow(hBtnRequestCode, FALSE);
                SetWindowTextW(hBtnRequestCode, L"Resend in 60s...");
            }

            SendMessageW(hDlg, WM_TIMER, 2, 0); // Sync initial control states

            // 16. Cancel / Close Button
            HWND hBtnCancel = CreateWindowExW(
                0, L"BUTTON", L"Close Whitelist Manager",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                15, 300, 520, 26, hDlg, (HMENU)IDCANCEL, GetModuleHandle(NULL), NULL
            );
            SendMessageW(hBtnCancel, WM_SETFONT, (WPARAM)s_pTrayUI->m_hFontText, TRUE);

            // Resize & Recenter window
            RECT rect;
            GetWindowRect(GetDesktopWindow(), &rect);
            int width = 560;
            int height = 375;
            SetWindowPos(
                hDlg, HWND_TOPMOST,
                (rect.right - width) / 2,
                (rect.bottom - height) / 2,
                width, height, SWP_SHOWWINDOW
            );

            // Proactively whitelist antigravity.exe
            if (!s_pTrayUI->m_profiles.empty()) {
                auto& p = s_pTrayUI->m_profiles[0];
                if (std::find(p.allowedApps.begin(), p.allowedApps.end(), L"antigravity.exe") == p.allowedApps.end()) {
                    p.allowedApps.push_back(L"antigravity.exe");
                    if (s_currentProfileIndex == 0) {
                        SendMessageW(hListAllowed, LB_ADDSTRING, 0, (LPARAM)L"antigravity.exe");
                    }
                }
            }

            s_isUpdatingFields = false;
            return FALSE;
        }

        case WM_TIMER: {
            if (wParam == 2) {
                bool isActive = s_pTrayUI->m_isStrictActive;
                
                s_isUpdatingFields = true;
                
                // Disable/enable profile editing during active session
                EnableWindow(hComboProfile, !isActive);
                EnableWindow(hBtnNew, !isActive);
                EnableWindow(hBtnDelete, !isActive);
                EnableWindow(hCheckWhitelist, !isActive);
                EnableWindow(hEditDuration, !isActive);
                EnableWindow(hEditVolume, !isActive);
                EnableWindow(hEditWallpaper, !isActive);
                EnableWindow(hBtnWallpaper, !isActive);
                EnableWindow(hListAllowed, !isActive);
                EnableWindow(hEditAdd, !isActive);
                EnableWindow(hBtnAdd, !isActive);
                EnableWindow(hBtnRemove, !isActive);
                
                // Toggle Start button vs inline Unlock controls
                bool wasActive = (IsWindowVisible(hBtnRequestCode) != FALSE);
                ShowWindow(hBtnStart, isActive ? SW_HIDE : SW_SHOW);
                ShowWindow(hBtnStop, SW_HIDE); // Always hide the old Stop button
                
                ShowWindow(hStaticActiveStatus, isActive ? SW_SHOW : SW_HIDE);
                ShowWindow(hBtnRequestCode, isActive ? SW_SHOW : SW_HIDE);
                ShowWindow(hStaticEnterCode, isActive ? SW_SHOW : SW_HIDE);
                ShowWindow(hEditUnlockCode, isActive ? SW_SHOW : SW_HIDE);
                ShowWindow(hBtnUnlock, isActive ? SW_SHOW : SW_HIDE);

                if (isActive) {
                    // Update default button and focus for unlock flow
                    SendMessageW(hDlg, DM_SETDEFID, 3016, 0);
                    // Auto-focus the code entry box when session becomes active
                    if (!wasActive) {
                        SetFocus(hEditUnlockCode);
                        SendMessageW(hEditUnlockCode, EM_SETSEL, 0, -1);
                    }

                    int mins = s_pTrayUI->m_timeRemainingSeconds / 60;
                    int secs = s_pTrayUI->m_timeRemainingSeconds % 60;
                    
                    std::wstringstream ss;
                    ss << L"ACTIVE FOCUS SESSION: " << s_pTrayUI->m_activeProfileName 
                       << L" (" << std::setw(2) << std::setfill(L'0') << mins 
                       << L":" << std::setw(2) << std::setfill(L'0') << secs << L" remaining)";
                    SetWindowTextW(hStaticActiveStatus, ss.str().c_str());

                    // Handle email cooldown timer
                    if (s_emailCooldownSeconds > 0) {
                        s_emailCooldownSeconds--;
                        if (s_emailCooldownSeconds > 0) {
                            std::wstring btnText = L"Resend in " + std::to_wstring(s_emailCooldownSeconds) + L"s...";
                            SetWindowTextW(hBtnRequestCode, btnText.c_str());
                            EnableWindow(hBtnRequestCode, FALSE);
                        } else {
                            SetWindowTextW(hBtnRequestCode, L"Request Code via Email");
                            EnableWindow(hBtnRequestCode, TRUE);
                        }
                    } else {
                        SetWindowTextW(hBtnRequestCode, L"Request Code via Email");
                        EnableWindow(hBtnRequestCode, TRUE);
                    }
                } else {
                    // Restore default button to Start when no session active
                    SendMessageW(hDlg, DM_SETDEFID, IDC_START_BTN, 0);
                }
                
                s_isUpdatingFields = false;
            }
            return 0;
        }

        case WM_TRIGGER_COOLDOWN: {
            s_emailCooldownSeconds = 60;
            EnableWindow(hBtnRequestCode, FALSE);
            SetWindowTextW(hBtnRequestCode, L"Resend in 60s...");
            return TRUE;
        }

        case WM_DESTROY: {
            s_pTrayUI->m_hDlgControlPanel = NULL;
            KillTimer(hDlg, 2);
            break;
        }

        case WM_CTLCOLORDLG: {
            return (INT_PTR)s_pTrayUI->m_hDarkBgBrush;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(230, 230, 235));
            SetBkColor(hdc, RGB(20, 20, 23));
            return (INT_PTR)s_pTrayUI->m_hDarkBgBrush;
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(40, 40, 45));
            return (INT_PTR)s_pTrayUI->m_hEditBgBrush;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(40, 40, 45));
            return (INT_PTR)s_pTrayUI->m_hEditBgBrush;
        }

        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(240, 240, 240));
            SetBkColor(hdc, RGB(20, 20, 23));
            return (INT_PTR)s_pTrayUI->m_hDarkBgBrush;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);

            // Handle edit fields changes instantly
            if (wmEvent == EN_CHANGE && !s_isUpdatingFields) {
                if (s_currentProfileIndex >= 0 && s_currentProfileIndex < (int)s_pTrayUI->m_profiles.size()) {
                    auto& profile = s_pTrayUI->m_profiles[s_currentProfileIndex];
                    if (wmId == IDC_DURATION_EDIT) {
                        wchar_t buf[32];
                        GetWindowTextW(hEditDuration, buf, 32);
                        try {
                            profile.durationMinutes = std::stoi(buf);
                        } catch (...) {
                            profile.durationMinutes = 0;
                        }
                        s_pTrayUI->saveProfilesToConfig();
                    }
                    else if (wmId == IDC_VOLUME_EDIT) {
                        wchar_t buf[32];
                        GetWindowTextW(hEditVolume, buf, 32);
                        try {
                            profile.volume = std::stoi(buf);
                            if (profile.volume < 0) profile.volume = 0;
                            if (profile.volume > 100) profile.volume = 100;
                        } catch (...) {
                            profile.volume = -1;
                        }
                        s_pTrayUI->saveProfilesToConfig();
                    }
                    else if (wmId == IDC_WALLPAPER_EDIT) {
                        wchar_t buf[MAX_PATH];
                        GetWindowTextW(hEditWallpaper, buf, MAX_PATH);
                        profile.wallpaperPath = buf;
                        s_pTrayUI->saveProfilesToConfig();
                    }
                }
            }

            // Dropdown selection change
            if (wmId == IDC_PROFILE_COMBO && wmEvent == CBN_SELCHANGE) {
                s_currentProfileIndex = (int)SendMessageW(hComboProfile, CB_GETCURSEL, 0, 0);
                if (s_currentProfileIndex < 0 || s_currentProfileIndex >= (int)s_pTrayUI->m_profiles.size()) return TRUE;
                
                s_isUpdatingFields = true;
                
                const auto& profile = s_pTrayUI->m_profiles[s_currentProfileIndex];
                updateListAndLabel(hDlg, hStaticList, hListAllowed, profile);

                // Checkbox status
                SendMessageW(hCheckWhitelist, BM_SETCHECK, 
                    profile.blockAllExceptAllowed ? BST_CHECKED : BST_UNCHECKED, 0);

                // Update input textboxes
                SetWindowTextW(hEditDuration, std::to_wstring(profile.durationMinutes).c_str());
                SetWindowTextW(hEditVolume, std::to_wstring(profile.volume).c_str());
                SetWindowTextW(hEditWallpaper, profile.wallpaperPath.c_str());

                s_isUpdatingFields = false;
                return TRUE;
            }

            // Whitelist Check Box Clicked
            if (wmId == IDC_WHITELIST_CHECK && wmEvent == BN_CLICKED) {
                bool checked = (SendMessageW(hCheckWhitelist, BM_GETCHECK, 0, 0) == BST_CHECKED);
                if (s_currentProfileIndex >= 0 && s_currentProfileIndex < (int)s_pTrayUI->m_profiles.size()) {
                    auto& profile = s_pTrayUI->m_profiles[s_currentProfileIndex];
                    profile.blockAllExceptAllowed = checked;
                    s_pTrayUI->saveProfilesToConfig();
                    s_pTrayUI->notifyDaemonReloadConfig();
                    updateListAndLabel(hDlg, hStaticList, hListAllowed, profile);
                }
                return TRUE;
            }

            switch (wmId) {
                case IDC_NEW_PROFILE_BTN: {
                    std::wstring newName = L"";
                    if (showInputDialog(hDlg, L"New Profile", L"Enter new profile name:", newName)) {
                        newName.erase(std::remove_if(newName.begin(), newName.end(), iswspace), newName.end());
                        if (newName.empty()) {
                            MessageBoxW(hDlg, L"Profile name cannot be empty.", L"Invalid Name", MB_OK | MB_ICONWARNING);
                            return TRUE;
                        }
                        
                        for (const auto& p : s_pTrayUI->m_profiles) {
                            if (_wcsicmp(p.name.c_str(), newName.c_str()) == 0) {
                                MessageBoxW(hDlg, L"A profile with this name already exists.", L"Duplicate Profile", MB_OK | MB_ICONWARNING);
                                return TRUE;
                            }
                        }

                        Profile newProfile;
                        newProfile.name = newName;
                        newProfile.blockAllExceptAllowed = true;
                        newProfile.durationMinutes = 25;
                        newProfile.volume = 30;
                        newProfile.wallpaperPath = L"";
                        newProfile.allowedApps = { L"notepad.exe", L"code.exe", L"msedge.exe", L"antigravity.exe" };
                        
                        s_pTrayUI->m_profiles.push_back(newProfile);
                        s_pTrayUI->saveProfilesToConfig();
                        
                        s_isUpdatingFields = true;
                        SendMessageW(hComboProfile, CB_ADDSTRING, 0, (LPARAM)newName.c_str());
                        int idx = (int)SendMessageW(hComboProfile, CB_FINDSTRINGEXACT, -1, (LPARAM)newName.c_str());
                        if (idx != CB_ERR) {
                            SendMessageW(hComboProfile, CB_SETCURSEL, idx, 0);
                            s_currentProfileIndex = idx;
                        }
                        
                        SendMessageW(hListAllowed, LB_RESETCONTENT, 0, 0);
                        for (const auto& app : newProfile.allowedApps) {
                            SendMessageW(hListAllowed, LB_ADDSTRING, 0, (LPARAM)app.c_str());
                        }

                        SendMessageW(hCheckWhitelist, BM_SETCHECK, BST_CHECKED, 0);
                        SetWindowTextW(hEditDuration, L"25");
                        SetWindowTextW(hEditVolume, L"30");
                        SetWindowTextW(hEditWallpaper, L"");
                        s_isUpdatingFields = false;

                        MessageBoxW(hDlg, L"Profile created successfully!", L"Success", MB_OK | MB_ICONINFORMATION);
                    }
                    return TRUE;
                }

                case IDC_DEL_PROFILE_BTN: {
                    if (s_pTrayUI->m_profiles.size() <= 1) {
                        MessageBoxW(hDlg, L"Cannot delete. You must keep at least one profile.", L"Cannot Delete", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }

                    if (s_currentProfileIndex >= 0 && s_currentProfileIndex < (int)s_pTrayUI->m_profiles.size()) {
                        std::wstring profName = s_pTrayUI->m_profiles[s_currentProfileIndex].name;
                        std::wstring confirmMsg = L"Are you sure you want to delete profile '" + profName + L"'?";
                        if (MessageBoxW(hDlg, confirmMsg.c_str(), L"Confirm Delete", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                            s_pTrayUI->m_profiles.erase(s_pTrayUI->m_profiles.begin() + s_currentProfileIndex);
                            s_pTrayUI->saveProfilesToConfig();

                            s_isUpdatingFields = true;
                            SendMessageW(hComboProfile, CB_RESETCONTENT, 0, 0);
                            for (const auto& p : s_pTrayUI->m_profiles) {
                                SendMessageW(hComboProfile, CB_ADDSTRING, 0, (LPARAM)p.name.c_str());
                            }

                            s_currentProfileIndex = 0;
                            SendMessageW(hComboProfile, CB_SETCURSEL, 0, 0);

                            const auto& profile = s_pTrayUI->m_profiles[s_currentProfileIndex];
                            updateListAndLabel(hDlg, hStaticList, hListAllowed, profile);

                            SendMessageW(hCheckWhitelist, BM_SETCHECK, 
                                profile.blockAllExceptAllowed ? BST_CHECKED : BST_UNCHECKED, 0);
                            
                            SetWindowTextW(hEditDuration, std::to_wstring(profile.durationMinutes).c_str());
                            SetWindowTextW(hEditVolume, std::to_wstring(profile.volume).c_str());
                            SetWindowTextW(hEditWallpaper, profile.wallpaperPath.c_str());
                            s_isUpdatingFields = false;

                            s_pTrayUI->notifyDaemonReloadConfig();
                        }
                    }
                    return TRUE;
                }

                case IDC_WALLPAPER_BTN: {
                    if (s_currentProfileIndex >= 0 && s_currentProfileIndex < (int)s_pTrayUI->m_profiles.size()) {
                        auto& profile = s_pTrayUI->m_profiles[s_currentProfileIndex];
                        
                        wchar_t szFile[MAX_PATH] = { 0 };
                        OPENFILENAMEW ofn;
                        ZeroMemory(&ofn, sizeof(ofn));
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner = hDlg;
                        ofn.lpstrFile = szFile;
                        ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
                        ofn.lpstrFilter = L"Image Files (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All Files (*.*)\0*.*\0";
                        ofn.nFilterIndex = 1;
                        ofn.lpstrFileTitle = NULL;
                        ofn.nMaxFileTitle = 0;
                        ofn.lpstrInitialDir = NULL;
                        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                        if (GetOpenFileNameW(&ofn) == TRUE) {
                            s_isUpdatingFields = true;
                            SetWindowTextW(hEditWallpaper, szFile);
                            profile.wallpaperPath = szFile;
                            s_isUpdatingFields = false;
                            
                            s_pTrayUI->saveProfilesToConfig();
                        }
                    }
                    return TRUE;
                }

                case IDC_ADD_BTN: {
                    wchar_t appName[128];
                    GetWindowTextW(hEditAdd, appName, 128);
                    std::wstring appStr(appName);
                    
                    appStr.erase(std::remove_if(appStr.begin(), appStr.end(), iswspace), appStr.end());
                    if (!appStr.empty() && s_currentProfileIndex >= 0 && s_currentProfileIndex < (int)s_pTrayUI->m_profiles.size()) {
                        auto& profile = s_pTrayUI->m_profiles[s_currentProfileIndex];
                        
                        if (profile.blockAllExceptAllowed) {
                            if (std::find(profile.allowedApps.begin(), profile.allowedApps.end(), appStr) == profile.allowedApps.end()) {
                                profile.allowedApps.push_back(appStr);
                                SendMessageW(hListAllowed, LB_ADDSTRING, 0, (LPARAM)appStr.c_str());
                                s_pTrayUI->saveProfilesToConfig();
                                s_pTrayUI->notifyDaemonReloadConfig();
                            }
                        } else {
                            if (std::find(profile.appsToClose.begin(), profile.appsToClose.end(), appStr) == profile.appsToClose.end()) {
                                profile.appsToClose.push_back(appStr);
                                SendMessageW(hListAllowed, LB_ADDSTRING, 0, (LPARAM)appStr.c_str());
                                s_pTrayUI->saveProfilesToConfig();
                                s_pTrayUI->notifyDaemonReloadConfig();
                            }
                        }
                    }
                    SetWindowTextW(hEditAdd, L"");
                    SetFocus(hEditAdd);
                    return TRUE;
                }
                
                case IDC_REMOVE_BTN: {
                    int selIdx = (int)SendMessageW(hListAllowed, LB_GETCURSEL, 0, 0);
                    if (selIdx != LB_ERR && s_currentProfileIndex >= 0 && s_currentProfileIndex < (int)s_pTrayUI->m_profiles.size()) {
                        auto& profile = s_pTrayUI->m_profiles[s_currentProfileIndex];
                        wchar_t buffer[128];
                        SendMessageW(hListAllowed, LB_GETTEXT, selIdx, (LPARAM)buffer);
                        std::wstring appStr(buffer);
                        
                        if (_wcsicmp(appStr.c_str(), L"antigravity.exe") == 0) {
                            MessageBoxW(hDlg, L"Cannot remove antigravity.exe. Protected for pair programming.", L"Access Denied", MB_OK | MB_ICONWARNING);
                            return TRUE;
                        }
                        
                        if (profile.blockAllExceptAllowed) {
                            auto it = std::find(profile.allowedApps.begin(), profile.allowedApps.end(), appStr);
                            if (it != profile.allowedApps.end()) {
                                profile.allowedApps.erase(it);
                                s_pTrayUI->saveProfilesToConfig();
                                s_pTrayUI->notifyDaemonReloadConfig();
                            }
                        } else {
                            auto it = std::find(profile.appsToClose.begin(), profile.appsToClose.end(), appStr);
                            if (it != profile.appsToClose.end()) {
                                profile.appsToClose.erase(it);
                                s_pTrayUI->saveProfilesToConfig();
                                s_pTrayUI->notifyDaemonReloadConfig();
                            }
                        }
                        SendMessageW(hListAllowed, LB_DELETESTRING, selIdx, 0);
                    }
                    return TRUE;
                }

                case IDC_START_BTN: {
                    if (s_currentProfileIndex >= 0 && s_currentProfileIndex < (int)s_pTrayUI->m_profiles.size()) {
                        s_pTrayUI->saveProfilesToConfig();
                        
                        const auto& profile = s_pTrayUI->m_profiles[s_currentProfileIndex];
                        if (s_pTrayUI->startSession(profile.name)) {
                            MessageBoxW(hDlg, L"Focus session triggered successfully. Whitelisting is engaged.", L"Focus Active", MB_OK | MB_ICONINFORMATION);
                            EndDialog(hDlg, IDOK);
                        } else {
                            MessageBoxW(hDlg, L"Failed to trigger focus session. Is the daemon running?", L"Error", MB_OK | MB_ICONERROR);
                        }
                    }
                    return TRUE;
                }

                case 3015: { // Request Unlock Code via Email
                    if (s_pTrayUI->requestUnlock()) {
                        s_emailCooldownSeconds = 60;
                        EnableWindow(hBtnRequestCode, FALSE);
                        SetWindowTextW(hBtnRequestCode, L"Resend in 60s...");
                        MessageBoxW(hDlg, L"A one-time unlock code has been dispatched to your configured email address. Check your inbox and enter the code below.", L"Unlock Code Sent", MB_OK | MB_ICONINFORMATION);
                        // Re-focus the code entry box after dismissing the dialog
                        SetFocus(hEditUnlockCode);
                    } else {
                        MessageBoxW(hDlg, L"Failed to request an unlock code. Is the Focus Engine daemon running? Check focus.log for details.", L"Request Failed", MB_OK | MB_ICONERROR);
                    }
                    return TRUE;
                }

                case 3016: { // Submit Unlock Code
                    wchar_t code[16];
                    GetWindowTextW(hEditUnlockCode, code, 16);
                    // Guard: reject empty submissions
                    if (wcslen(code) == 0) {
                        SetFocus(hEditUnlockCode);
                        return TRUE;
                    }
                    if (s_pTrayUI->submitUnlockCode(code)) {
                        MessageBoxW(hDlg, L"Session successfully unlocked. System environment restored.", L"Unlocked!", MB_OK | MB_ICONINFORMATION);
                        SetWindowTextW(hEditUnlockCode, L"");
                        // Trigger immediate refresh of control states
                        SendMessageW(hDlg, WM_TIMER, 2, 0);
                    } else {
                        MessageBoxW(hDlg, L"Incorrect unlock code. Please check your email and try again.", L"Verification Failed", MB_OK | MB_ICONERROR | MB_TOPMOST);
                        SetWindowTextW(hEditUnlockCode, L"");
                        SetFocus(hEditUnlockCode);
                    }
                    return TRUE;
                }

                case IDC_STOP_BTN: {
                    // Deprecated: old Stop button action is replaced by inline fields
                    return TRUE;
                }

                case IDCANCEL: {
                    EndDialog(hDlg, IDCANCEL);
                    return TRUE;
                }
            }
            break;
        }
    }
    return FALSE;
}

void TrayUI::openControlPanel() {
    if (m_hDlgControlPanel != NULL) {
        ShowWindow(m_hDlgControlPanel, SW_RESTORE);
        SetForegroundWindow(m_hDlgControlPanel);
        SetActiveWindow(m_hDlgControlPanel);
        if (s_autoRequestUnlock) {
            s_autoRequestUnlock = false;
            requestUnlock();
            PostMessageW(m_hDlgControlPanel, WM_TRIGGER_COOLDOWN, 0, 0);
        }
        return;
    }

    LPDLGTEMPLATE lpd = createControlPanelDialogTemplate();
    if (lpd) {
        DialogBoxIndirectParamW(
            m_hInstance,
            lpd,
            NULL,
            ControlPanelDlgProc,
            0
        );
        LocalFree(lpd);
    }
}

void TrayUI::saveProfilesToConfig() {
    std::wstring configPath = getAppDirectory() + L"\\config.json";
    
    std::wstringstream ss;
    ss << L"{\n";
    ss << L"  \"smtp\": {\n";
    ss << L"    \"smtpUser\": \"" << escapeJsonString(s_localSmtp.smtpUser) << L"\",\n";
    ss << L"    \"smtpPassword\": \"" << escapeJsonString(s_localSmtp.smtpPassword) << L"\",\n";
    ss << L"    \"emailTo1\": \"" << escapeJsonString(s_localSmtp.emailTo1) << L"\",\n";
    ss << L"    \"emailTo2\": \"" << escapeJsonString(s_localSmtp.emailTo2) << L"\"\n";
    ss << L"  },\n";
    ss << L"  \"profiles\": [\n";
    
    for (size_t i = 0; i < m_profiles.size(); ++i) {
        const auto& p = m_profiles[i];
        ss << L"    {\n";
        ss << L"      \"name\": \"" << escapeJsonString(p.name) << L"\",\n";
        ss << L"      \"blockAllExceptAllowed\": " << (p.blockAllExceptAllowed ? L"true" : L"false") << L",\n";
        
        ss << L"      \"allowedApps\": [\n";
        for (size_t j = 0; j < p.allowedApps.size(); ++j) {
            ss << L"        \"" << escapeJsonString(p.allowedApps[j]) << L"\"";
            if (j + 1 < p.allowedApps.size()) ss << L",";
            ss << L"\n";
        }
        ss << L"      ],\n";
        
        ss << L"      \"appsToClose\": [\n";
        for (size_t j = 0; j < p.appsToClose.size(); ++j) {
            ss << L"        \"" << escapeJsonString(p.appsToClose[j]) << L"\"";
            if (j + 1 < p.appsToClose.size()) ss << L",";
            ss << L"\n";
        }
        ss << L"      ],\n";
        
        ss << L"      \"appsToLaunch\": [\n";
        for (size_t j = 0; j < p.appsToLaunch.size(); ++j) {
            ss << L"        \"" << escapeJsonString(p.appsToLaunch[j]) << L"\"";
            if (j + 1 < p.appsToLaunch.size()) ss << L",";
            ss << L"\n";
        }
        ss << L"      ],\n";
        
        ss << L"      \"wallpaperPath\": \"" << escapeJsonString(p.wallpaperPath) << L"\",\n";
        ss << L"      \"volume\": " << p.volume << L",\n";
        ss << L"      \"durationMinutes\": " << p.durationMinutes << L"\n";
        
        ss << L"    }";
        if (i + 1 < m_profiles.size()) ss << L",";
        ss << L"\n";
    }
    ss << L"  ]\n";
    ss << L"}\n";
    
    std::string utf8Json = wstringToString(ss.str());
    std::wstring tempPath = configPath + L".tmp";
    std::ofstream f(tempPath.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
    if (f.is_open()) {
        f.write(utf8Json.c_str(), utf8Json.size());
        f.close();
        MoveFileExW(tempPath.c_str(), configPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
}

LRESULT TrayUI::handleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_STATUS_UPDATE: {
            StatusUpdateData* pData = reinterpret_cast<StatusUpdateData*>(lParam);
            if (pData) {
                bool wasStrict = m_isStrictActive;
                m_isStrictActive = pData->isStrictActive;
                m_timeRemainingSeconds = pData->timeRemainingSeconds;
                m_activeProfileName = pData->profileName;

                // Visual notification on strict transition
                if (m_isStrictActive && !wasStrict) {
                    m_nid.uFlags = NIF_INFO;
                    wcscpy_s(m_nid.szInfoTitle, L"Focus Mode Active");
                    wcscpy_s(m_nid.szInfo, L"Strict session engaged. Double-click tray icon or run Focus.exe to open Control Panel and enter unlock code.");
                    m_nid.dwInfoFlags = NIIF_INFO;
                    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
                }
                else if (!m_isStrictActive && wasStrict) {
                    m_nid.uFlags = NIF_INFO;
                    wcscpy_s(m_nid.szInfoTitle, L"Focus Session Expired");
                    wcscpy_s(m_nid.szInfo, L"Environment restored. Great job!");
                    m_nid.dwInfoFlags = NIIF_INFO;
                    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
                }

                // Update tooltip
                m_nid.uFlags = NIF_TIP;
                if (m_isStrictActive) {
                    int mins = m_timeRemainingSeconds / 60;
                    int secs = m_timeRemainingSeconds % 60;
                    std::wstringstream ss;
                    ss << L"Focus Mode Active - " << m_activeProfileName << L" (" 
                       << std::setw(2) << std::setfill(L'0') << mins << L":"
                       << std::setw(2) << std::setfill(L'0') << secs << L")";
                    wcscpy_s(m_nid.szTip, ss.str().c_str());
                } else {
                    wcscpy_s(m_nid.szTip, L"Focus Mode Engine - Idle");
                }
                Shell_NotifyIconW(NIM_MODIFY, &m_nid);

                delete pData;
            }
            return 0;
        }
        case WM_TRAYICON: {
            if (lParam == WM_RBUTTONUP) {
                showContextMenu();
            }
            else if (lParam == WM_LBUTTONDBLCLK) {
                openControlPanel(); // Double click tray icon to open Control Panel!
            }
            return 0;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            
            // Check if selected a profile from Start menu
            if (wmId >= ID_MENU_PROFILE_START && wmId < (ID_MENU_PROFILE_START + static_cast<int>(m_profiles.size()))) {
                int profileIdx = wmId - ID_MENU_PROFILE_START;
                startSession(m_profiles[profileIdx].name);
                return 0;
            }

            switch (wmId) {
                case ID_MENU_CONTROL_PANEL: {
                    openControlPanel();
                    return 0;
                }
                case ID_MENU_EXIT: {
                    if (m_isStrictActive) {
                        MessageBoxW(NULL, L"Strict session is active. Casually exiting is blocked!", L"Exit Blocked", MB_OK | MB_ICONERROR);
                    } else {
                        // Authorized full quit
                        IpcMessage sendMsg;
                        ZeroMemory(&sendMsg, sizeof(IpcMessage));
                        sendMsg.type = IpcCommandType::ForceQuit;
                        
                        IpcMessage reply;
                        ZeroMemory(&reply, sizeof(IpcMessage));
                        DWORD bytesRead = 0;
                        
                        CallNamedPipeW(
                            FOCUS_PIPE_NAME,
                            (LPVOID)&sendMsg,
                            sizeof(IpcMessage),
                            &reply,
                            sizeof(IpcMessage),
                            &bytesRead,
                            2000
                        );
                        
                        PostQuitMessage(0);
                    }
                    return 0;
                }
                case ID_MENU_UNLOCK: {
                    s_autoRequestUnlock = true;
                    openControlPanel();
                    return 0;
                }
                case ID_MENU_ENTER_CODE: {
                    std::wstring code = L"";
                    if (showInputDialog(NULL, L"Enter Unlock Code", L"Enter the code sent to your email:", code)) {
                        code.erase(std::remove_if(code.begin(), code.end(), iswspace), code.end());
                        if (code.empty()) {
                            MessageBoxW(NULL, L"Unlock code cannot be empty.", L"Invalid Code", MB_OK | MB_ICONWARNING | MB_TOPMOST);
                            return 0;
                        }
                        if (submitUnlockCode(code)) {
                            MessageBoxW(NULL, L"Session successfully unlocked. System environment restored.", L"Unlocked!", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
                        } else {
                            MessageBoxW(NULL, L"Incorrect unlock code. Please check your email and try again.", L"Verification Failed", MB_OK | MB_ICONERROR | MB_TOPMOST);
                        }
                    }
                    return 0;
                }
            }
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
