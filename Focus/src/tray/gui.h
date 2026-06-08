#pragma once
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <atomic>
#include "../common/ipc.h"
#include "../common/config.h"

#define WM_TRAYICON (WM_USER + 1)
#define WM_TRIGGER_COOLDOWN (WM_USER + 100)
#define ID_TRAY_ICON 1001

// UI Control IDs
#define ID_MENU_PROFILE_START 2000
#define ID_MENU_EXIT          1002
#define ID_MENU_UNLOCK        1003
#define ID_MENU_STATUS        1004
#define ID_MENU_CONTROL_PANEL 1005
#define ID_MENU_ENTER_CODE    1006

// Custom Dialog Control IDs
#define IDC_PROFILE_COMBO     3001
#define IDC_WHITELIST_LIST    3002
#define IDC_ADD_EDIT          3003
#define IDC_ADD_BTN           3004
#define IDC_REMOVE_BTN        3005
#define IDC_START_BTN         3006
#define IDC_STOP_BTN          3007
#define IDC_WHITELIST_CHECK   3008
#define IDC_NEW_PROFILE_BTN   3009
#define IDC_DEL_PROFILE_BTN   3010
#define IDC_DURATION_EDIT     3011
#define IDC_VOLUME_EDIT       3012
#define IDC_WALLPAPER_EDIT    3013
#define IDC_WALLPAPER_BTN     3014


class TrayUI {
public:
    TrayUI(HINSTANCE hInstance);
    ~TrayUI();

    bool initialize();
    void run();

    HWND getWindowHandle() const { return m_hwnd; }

    // Message handler
    LRESULT handleMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

private:
    HINSTANCE m_hInstance;
    HWND m_hwnd;
    HWND m_hDlgControlPanel;
    NOTIFYICONDATAW m_nid;
    std::vector<Profile> m_profiles;
    bool m_isStrictActive;
    std::wstring m_activeProfileName;
    int m_timeRemainingSeconds;
    HBRUSH m_hDarkBgBrush;
    HBRUSH m_hAccentBrush;
    HBRUSH m_hEditBgBrush;
    HFONT m_hFontTitle;
    HFONT m_hFontText;

    // Polling thread members
    HANDLE m_hPollThread;
    std::atomic<bool> m_pollThreadRunning;

    // Window and tray icon setup
    bool createHiddenWindow();
    void setupTrayIcon();
    void removeTrayIcon();
    
    // Popup menus and submenus
    void showContextMenu();
    void refreshProfiles();

    // IPC Client Helper
    bool queryStatus();
    void queryStatusAsync();
    bool startSession(const std::wstring& profileName);
    bool submitUnlockCode(const std::wstring& code);
    bool requestUnlock();
    bool notifyDaemonReloadConfig();

public:
    bool isPollThreadRunning() const { return m_pollThreadRunning; }

private:
    // Custom owner-draw and dialog helpers
    static INT_PTR CALLBACK ControlPanelDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK InputDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI pollThreadFunc(LPVOID lpParam);
    
    void openControlPanel();
    static bool showInputDialog(HWND parent, const std::wstring& title, const std::wstring& prompt, std::wstring& outValue);

    
    // Helper to save current local changes of profiles whitelists back to config.json
    void saveProfilesToConfig();
};
