#pragma once
#include <windows.h>
#include <string>
#include <functional>

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    bool Initialize(HINSTANCE hInstance);
    void Shutdown();

    void SetChangeVideoCallback(std::function<void(const std::wstring&)> cb);
    void SetTogglePauseCallback(std::function<void(bool)> cb);
    void SetExitCallback(std::function<void()> cb);

    // New Phase 7 Callbacks
    void SetAddVideoCallback(std::function<void(const std::wstring&)> cb);
    void SetClearPlaylistCallback(std::function<void()> cb);
    void SetManagePlaylistCallback(std::function<void()> cb);
    void SetNextVideoCallback(std::function<void()> cb);
    void SetIntervalCallback(std::function<void(int)> cb);

    void UpdatePauseState(bool isPaused);
    void UpdateRotationInterval(int minutes);
    void UpdateHasPlaylist(bool hasPlaylist);

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    void ShowContextMenu();
    void OpenVideoDialog();
    void OpenAddVideoDialog();
    void RecreateTrayIcon();

    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;
    bool m_isPaused = false;
    int m_rotationIntervalMinutes = 0;
    bool m_hasPlaylist = false;

    std::function<void(const std::wstring&)> m_onChangeVideo;
    std::function<void(bool)> m_onTogglePause;
    std::function<void()> m_onExit;

    // New Phase 7 Callbacks
    std::function<void(const std::wstring&)> m_onAddVideo;
    std::function<void()> m_onClearPlaylist;
    std::function<void()> m_onManagePlaylist;
    std::function<void()> m_onNextVideo;
    std::function<void(int)> m_onSetInterval;
};
