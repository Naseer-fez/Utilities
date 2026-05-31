#include "PlayerGUI.h"
#include <commctrl.h>
#include <filesystem>

namespace fs = std::filesystem;

#define CLR_DARK_BG       RGB(24, 24, 32)
#define CLR_DARK_CTRL     RGB(36, 36, 48)
#define CLR_DARK_LIST     RGB(30, 30, 42)
#define CLR_TEXT_PRIMARY   RGB(230, 230, 240)
#define CLR_TEXT_SECONDARY RGB(160, 160, 180)
#define CLR_ACCENT         RGB(120, 90, 255)

static std::wstring FormatTime(int ms) {
    int totalSec = ms / 1000;
    int min = totalSec / 60;
    int sec = totalSec % 60;
    wchar_t buf[32];
    swprintf_s(buf, L"%02d:%02d", min, sec);
    return std::wstring(buf);
}

PlayerGUI::PlayerGUI(HINSTANCE hInstance, HWND parent, const std::wstring& appDir) 
    : hInst(hInstance), hWnd(parent), appDirectory(appDir), hBmpLogo(NULL), 
      playlistVisible(true), hTrackbar(NULL), hLabelCurrentTime(NULL), 
      hLabelTotalTime(NULL), hPlaylistListBox(NULL), hSearchEdit(NULL), hBtnSearch(NULL),
      hTrackbarVolume(NULL), hLabelVolume(NULL), hLabelVolumeIcon(NULL),
      hBtnMenuFile(NULL), hBtnMenuView(NULL), hBtnMenuWindow(NULL),
      hBtnPlayPause(NULL), hBtnNext(NULL), hBtnPrev(NULL), hBtnShuffle(NULL),
      hBtnPlaylist(NULL), hLabelSong(NULL), hPicLogo(NULL) {
      
    hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
                        
    hFontSmall = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
                        
    hBrushDarkBg   = CreateSolidBrush(CLR_DARK_BG);
    hBrushDarkCtrl = CreateSolidBrush(CLR_DARK_CTRL);
    hBrushDarkList = CreateSolidBrush(CLR_DARK_LIST);
}

PlayerGUI::~PlayerGUI() {
    if (hFont) DeleteObject(hFont);
    if (hFontSmall) DeleteObject(hFontSmall);
    if (hBmpLogo) DeleteObject(hBmpLogo);
    if (hBrushDarkBg) DeleteObject(hBrushDarkBg);
    if (hBrushDarkCtrl) DeleteObject(hBrushDarkCtrl);
    if (hBrushDarkList) DeleteObject(hBrushDarkList);
}

void PlayerGUI::createControls() {
    // 1. Custom Menu Bar Buttons (Owner-Drawn for flat look)
    hBtnMenuFile = CreateWindowW(L"BUTTON", L"File", 
                                WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 
                                10, 5, 50, 25, hWnd, (HMENU)ID_BTN_MENU_FILE, hInst, NULL);
    hBtnMenuView = CreateWindowW(L"BUTTON", L"View", 
                                WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 
                                70, 5, 50, 25, hWnd, (HMENU)ID_BTN_MENU_VIEW, hInst, NULL);
    hBtnMenuWindow = CreateWindowW(L"BUTTON", L"Window", 
                                   WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 
                                   130, 5, 70, 25, hWnd, (HMENU)ID_BTN_MENU_WINDOW, hInst, NULL);

    // 2. Current Song Label
    hLabelSong = CreateWindowW(L"STATIC", L"No folder selected...", 
                               WS_VISIBLE | WS_CHILD | SS_CENTER | SS_NOPREFIX, 
                               10, 50, 540, 30, hWnd, (HMENU)ID_LABEL_SONG, hInst, NULL);

    // 3. Logo Image Display (Loaded via absolute path to ensure correct loading)
    std::wstring logoPath = appDirectory + L"\\assets\\logo.bmp";
    hBmpLogo = (HBITMAP)LoadImageW(NULL, logoPath.c_str(), IMAGE_BITMAP, 300, 300, LR_LOADFROMFILE);
    hPicLogo = CreateWindowW(L"STATIC", NULL, 
                             WS_VISIBLE | WS_CHILD | SS_BITMAP | SS_REALSIZECONTROL, 
                             120, 90, 300, 300, hWnd, NULL, hInst, NULL);
    if (hBmpLogo) {
        SendMessageW(hPicLogo, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)hBmpLogo);
    }

    // 4. Playback Controls Buttons (Owner-Drawn for beautiful dark styling)
    hBtnPrev = CreateWindowW(L"BUTTON", L"PREV", 
                             WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 
                             70, 405, 50, 50, hWnd, (HMENU)ID_BTN_PREV, hInst, NULL);
    hBtnPlayPause = CreateWindowW(L"BUTTON", L"▶", 
                                  WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 
                                  135, 400, 60, 60, hWnd, (HMENU)ID_BTN_PLAY_PAUSE, hInst, NULL);
    hBtnNext = CreateWindowW(L"BUTTON", L"NEXT", 
                             WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 
                             210, 405, 50, 50, hWnd, (HMENU)ID_BTN_NEXT, hInst, NULL);
    hBtnShuffle = CreateWindowW(L"BUTTON", L"SHUFFLE", 
                                WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 
                                280, 410, 90, 40, hWnd, (HMENU)ID_BTN_SHUFFLE, hInst, NULL);
    hBtnPlaylist = CreateWindowW(L"BUTTON", L"PLAYLIST", 
                                 WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 
                                 385, 410, 90, 40, hWnd, (HMENU)ID_BTN_PLAYLIST, hInst, NULL);

    // 5. Timeline Trackbar
    hTrackbar = CreateWindowW(TRACKBAR_CLASSW, L"", 
                              WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_NOTICKS, 
                              20, 475, 520, 30, hWnd, (HMENU)ID_TRACKBAR, hInst, NULL);

    // 6. Timeline Labels
    hLabelCurrentTime = CreateWindowW(L"STATIC", L"00:00", 
                                     WS_VISIBLE | WS_CHILD | SS_LEFT, 
                                     20, 505, 60, 20, hWnd, (HMENU)ID_LABEL_CUR_TIME, hInst, NULL);
    hLabelTotalTime = CreateWindowW(L"STATIC", L"00:00", 
                                   WS_VISIBLE | WS_CHILD | SS_RIGHT, 
                                   480, 505, 60, 20, hWnd, (HMENU)ID_LABEL_TOT_TIME, hInst, NULL);

    // 7. Search Bar & Clear Button (Visible by Default)
    hSearchEdit = CreateWindowW(L"EDIT", L"", 
                                WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 
                                560, 45, 175, 30, hWnd, (HMENU)ID_EDIT_SEARCH, hInst, NULL);
    hBtnSearch = CreateWindowW(L"BUTTON", L"🔍", 
                               WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 
                               740, 45, 40, 30, hWnd, (HMENU)ID_BTN_SEARCH, hInst, NULL);

    // 8. Playlist Sidebar ListBox (Visible by Default)
    hPlaylistListBox = CreateWindowW(L"LISTBOX", L"", 
                                    WS_VISIBLE | WS_CHILD | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY | WS_VSCROLL | WS_BORDER, 
                                    560, 85, 220, 460, hWnd, (HMENU)ID_LISTBOX_PLAYLIST, hInst, NULL);

    // 9. Volume Controls
    hLabelVolumeIcon = CreateWindowW(L"STATIC", L"🔊", 
                                     WS_VISIBLE | WS_CHILD | SS_LEFT, 
                                     20, 535, 20, 20, hWnd, NULL, hInst, NULL);
    hTrackbarVolume = CreateWindowW(TRACKBAR_CLASSW, L"", 
                                    WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_NOTICKS, 
                                    45, 530, 120, 30, hWnd, (HMENU)ID_TRACKBAR_VOLUME, hInst, NULL);
    hLabelVolume = CreateWindowW(L"STATIC", L"80%", 
                                 WS_VISIBLE | WS_CHILD | SS_LEFT, 
                                 175, 535, 45, 20, hWnd, (HMENU)ID_LABEL_VOLUME, hInst, NULL);

    // Configure Volume Slider
    SendMessageW(hTrackbarVolume, TBM_SETRANGEMIN, TRUE, 0);
    SendMessageW(hTrackbarVolume, TBM_SETRANGEMAX, TRUE, 100);
    SendMessageW(hTrackbarVolume, TBM_SETPOS, TRUE, 80);

    // Apply custom fonts to labels, search bar, volume, and list
    SendMessageW(hLabelSong, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hTrackbar, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hLabelCurrentTime, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
    SendMessageW(hLabelTotalTime, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
    SendMessageW(hSearchEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hBtnSearch, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hPlaylistListBox, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hLabelVolumeIcon, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hTrackbarVolume, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageW(hLabelVolume, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
}

void PlayerGUI::updateSongLabel(const std::wstring& songName) {
    SetWindowTextW(hLabelSong, songName.c_str());
}

void PlayerGUI::updatePlayPauseButton(bool isPlaying) {
    SetWindowTextW(hBtnPlayPause, isPlaying ? L"⏸" : L"▶");
    InvalidateRect(hBtnPlayPause, NULL, TRUE);
}

void PlayerGUI::updateShuffleButton(bool enabled) {
    // Redraw button to update accent background color based on active state; text remains SHUFFLE
    InvalidateRect(hBtnShuffle, NULL, TRUE);
}

void PlayerGUI::togglePlaylist() {
    playlistVisible = !playlistVisible;
    if (playlistVisible) {
        ShowWindow(hPlaylistListBox, SW_SHOW);
        ShowWindow(hSearchEdit, SW_SHOW);
        ShowWindow(hBtnSearch, SW_SHOW);
        SetWindowPos(hWnd, NULL, 0, 0, 800, 600, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        ShowWindow(hPlaylistListBox, SW_HIDE);
        ShowWindow(hSearchEdit, SW_HIDE);
        ShowWindow(hBtnSearch, SW_HIDE);
        SetWindowPos(hWnd, NULL, 0, 0, 560, 600, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void PlayerGUI::populatePlaylist(const std::vector<std::wstring>& playlist) {
    SendMessageW(hPlaylistListBox, LB_RESETCONTENT, 0, 0);
    for (const auto& songPath : playlist) {
        fs::path p(songPath);
        SendMessageW(hPlaylistListBox, LB_ADDSTRING, 0, (LPARAM)p.stem().wstring().c_str());
    }
}

void PlayerGUI::setSelectedSongIndex(int index) {
    SendMessageW(hPlaylistListBox, LB_SETCURSEL, index, 0);
}

void PlayerGUI::updateTimeLabels(int currentMs, int totalMs) {
    if (hLabelCurrentTime) {
        SetWindowTextW(hLabelCurrentTime, FormatTime(currentMs).c_str());
    }
    if (hLabelTotalTime) {
        SetWindowTextW(hLabelTotalTime, FormatTime(totalMs).c_str());
    }
}

void PlayerGUI::setTrackbarRange(int maxMs) {
    if (hTrackbar) {
        SendMessageW(hTrackbar, TBM_SETRANGEMIN, TRUE, 0);
        SendMessageW(hTrackbar, TBM_SETRANGEMAX, TRUE, maxMs);
        SendMessageW(hTrackbar, TBM_SETPOS, TRUE, 0);
        InvalidateRect(hTrackbar, NULL, FALSE);
    }
}