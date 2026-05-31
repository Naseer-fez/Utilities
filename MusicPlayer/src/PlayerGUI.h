#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Control IDs
#define ID_BTN_PLAY_PAUSE    102
#define ID_BTN_NEXT          103
#define ID_BTN_PREV          104
#define ID_LABEL_SONG        110
#define ID_BTN_SHUFFLE       111
#define ID_TRACKBAR          112
#define ID_LISTBOX_PLAYLIST  113
#define ID_LABEL_CUR_TIME    114
#define ID_LABEL_TOT_TIME    115
#define ID_BTN_PLAYLIST      116
#define ID_EDIT_SEARCH       117
#define ID_BTN_SEARCH        118
#define ID_TRACKBAR_VOLUME   119
#define ID_LABEL_VOLUME      123

// Custom Menu Bar Button IDs
#define ID_BTN_MENU_FILE     120
#define ID_BTN_MENU_VIEW     121
#define ID_BTN_MENU_WINDOW   122

// Menu Option Command IDs
#define ID_MENU_SELECT_FOLDER 201
#define ID_MENU_RECENT_FOLDER 202
#define ID_MENU_EXIT          203
#define ID_MENU_PLAYLIST      204
#define ID_MENU_ABOUT         205

class PlayerGUI {
public:
    PlayerGUI(HINSTANCE hInstance, HWND parent, const std::wstring& appDir);
    ~PlayerGUI();

    void createControls();
    void updateSongLabel(const std::wstring& songName);
    void updatePlayPauseButton(bool isPlaying);
    void updateShuffleButton(bool enabled);
    void togglePlaylist();
    void populatePlaylist(const std::vector<std::wstring>& playlist);
    void setSelectedSongIndex(int index);
    
    // Retrieve HWNDs for custom message handling or font setting
    HWND getMainWindow() const { return hWnd; }
    HWND getTrackbar() const { return hTrackbar; }
    HWND getPlaylistListBox() const { return hPlaylistListBox; }
    HWND getSearchEdit() const { return hSearchEdit; }
    HWND getSearchButton() const { return hBtnSearch; }
    HWND getTrackbarVolume() const { return hTrackbarVolume; }
    HWND getVolumeLabel() const { return hLabelVolume; }
    HWND getPrevButton() const { return hBtnPrev; }
    HWND getPlayPauseButton() const { return hBtnPlayPause; }
    HWND getNextButton() const { return hBtnNext; }
    HWND getShuffleButton() const { return hBtnShuffle; }
    HWND getPlaylistButton() const { return hBtnPlaylist; }
    void updateTimeLabels(int currentMs, int totalMs);
    bool isPlaylistVisible() const { return playlistVisible; }

    // Dark theme helpers
    HBRUSH getDarkBgBrush() const { return hBrushDarkBg; }
    HBRUSH getDarkCtrlBrush() const { return hBrushDarkCtrl; }
    HBRUSH getDarkListBrush() const { return hBrushDarkList; }

    // Set trackbar range once per song (not every tick)
    void setTrackbarRange(int maxMs);

private:
    HINSTANCE hInst;
    HWND hWnd;
    std::wstring appDirectory;
    
    // Controls
    HWND hBtnMenuFile;
    HWND hBtnMenuView;
    HWND hBtnMenuWindow;

    HWND hBtnPlayPause;
    HWND hBtnNext;
    HWND hBtnPrev;
    HWND hBtnShuffle;
    HWND hBtnPlaylist;
    HWND hLabelSong;
    HWND hPicLogo;
    
    HWND hTrackbar;
    HWND hLabelCurrentTime;
    HWND hLabelTotalTime;
    HWND hPlaylistListBox;
    HWND hSearchEdit;
    HWND hBtnSearch;
    HWND hTrackbarVolume;
    HWND hLabelVolume;
    HWND hLabelVolumeIcon;
    
    bool playlistVisible;
    
    HBITMAP hBmpLogo;
    HFONT hFont;
    HFONT hFontSmall;

    // Dark theme brushes
    HBRUSH hBrushDarkBg;
    HBRUSH hBrushDarkCtrl;
    HBRUSH hBrushDarkList;
};