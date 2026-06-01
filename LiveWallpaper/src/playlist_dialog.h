#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

class PlaylistDialog {
public:
    PlaylistDialog();
    ~PlaylistDialog();

    bool Initialize(HINSTANCE hInstance);
    void Show(HWND parentWindow, const std::vector<std::wstring>& currentPlaylist, size_t currentIndex);
    void Hide();

    void SetOnPlaylistUpdatedCallback(std::function<void(const std::vector<std::wstring>&, size_t)> cb);

private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    void OnInitDialog();
    void OnAddVideo();
    void OnRemoveVideo();
    void OnMoveUp();
    void OnMoveDown();
    void OnSave();
    void OnCancel();
    void RefreshListBox();

    HWND m_hWnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    HFONT m_hFont = nullptr;
    std::vector<std::wstring> m_playlist;
    size_t m_currentIndex = 0;
    
    // Backups for discarding changes
    std::vector<std::wstring> m_originalPlaylist;
    size_t m_originalIndex = 0;
    
    std::function<void(const std::vector<std::wstring>&, size_t)> m_onPlaylistUpdated;
};
