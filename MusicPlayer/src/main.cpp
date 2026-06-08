#include <windows.h>
#include <shlobj.h>
#include <commctrl.h>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <thread>
#include <atomic>

#include "PlayerGUI.h"
#include "AudioEngine.h"
#include "Shuffler.h"

#define TIMER_ID 1
#define TIMER_INTERVAL 100

namespace fs = std::filesystem;

#include <cwctype>

// Global Instances
std::atomic<int> g_SearchCounter{0};
AudioEngine g_Audio;
Shuffler g_Shuffler;
PlayerGUI* g_GUI = nullptr;
std::wstring g_CurrentSongPath;
std::wstring g_AppDir;
bool g_IsPlaying = false;
bool g_IsDraggingTrackbar = false;
std::vector<std::wstring> g_DisplayedPlaylist;

// Dark theme colors
#define CLR_DARK_BG       RGB(24, 24, 32)
#define CLR_DARK_CTRL     RGB(36, 36, 48)
#define CLR_DARK_LIST     RGB(30, 30, 42)
#define CLR_TEXT_PRIMARY   RGB(230, 230, 240)
#define CLR_TEXT_SECONDARY RGB(160, 160, 180)
#define CLR_ACCENT         RGB(120, 90, 255)

// Helper to convert wstring to lowercase
std::wstring ToLower(const std::wstring& str) {
    std::wstring result = str;
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t c) {
        return std::towlower(c);
    });
    return result;
}

// Fuzzy matching scorer
// Returns a score: -1 if no match, >= 0 if matches (higher is better)
int FuzzyMatchScore(const std::wstring& target, const std::wstring& query) {
    if (query.empty()) return 100;
    
    std::wstring t = ToLower(target);
    std::wstring q = ToLower(query);
    
    size_t tIdx = 0;
    size_t qIdx = 0;
    int score = 0;
    int consecutive = 0;
    
    std::vector<size_t> matchIndices;
    
    // Check if characters exist in order
    while (tIdx < t.length() && qIdx < q.length()) {
        if (t[tIdx] == q[qIdx]) {
            matchIndices.push_back(tIdx);
            
            // Base match points
            int charScore = 10;
            
            // Start of string bonus
            if (tIdx == 0) {
                charScore += 80;
            }
            // Start of word bonus (after space, hyphen, underscore, bracket, etc.)
            else {
                wchar_t prev = t[tIdx - 1];
                if (prev == L' ' || prev == L'-' || prev == L'_' || prev == L'(' || prev == L'[' || prev == L'.') {
                    charScore += 50;
                }
            }
            
            // Consecutive character bonus
            if (consecutive > 0) {
                charScore += (consecutive * 30);
            }
            
            score += charScore;
            consecutive++;
            qIdx++;
        } else {
            consecutive = 0;
        }
        tIdx++;
    }
    
    // If not all characters in query were found, no match
    if (qIdx < q.length()) {
        return -1;
    }
    
    // Shortest match span bonus (closer characters = better)
    if (!matchIndices.empty()) {
        size_t span = matchIndices.back() - matchIndices.front() + 1;
        if (span >= q.length()) {
            int spanPenalty = static_cast<int>(span - q.length()) * 2;
            score = (std::max)(1, score - spanPenalty);
        }
    }
    
    // Length difference penalty
    int lenDiff = static_cast<int>(t.length() - q.length());
    score = (std::max)(1, score - lenDiff);
    
    return score;
}

// Highlight the currently playing song in the playlist listbox
void UpdatePlaylistHighlight() {
    if (!g_GUI || g_CurrentSongPath.empty()) return;
    auto it = std::find(g_DisplayedPlaylist.begin(), g_DisplayedPlaylist.end(), g_CurrentSongPath);
    if (it != g_DisplayedPlaylist.end()) {
        int idx = static_cast<int>(std::distance(g_DisplayedPlaylist.begin(), it));
        g_GUI->setSelectedSongIndex(idx);
    } else {
        g_GUI->setSelectedSongIndex(-1);
    }
}

// Filters the displayed playlist using the fuzzy match scoring algorithm (Asynchronous)
void FilterPlaylist(const std::wstring& query) {
    if (!g_GUI) return;
    
    int currentSearch = ++g_SearchCounter;
    std::thread([query, currentSearch]() {
        const auto& original = g_Shuffler.getPlaylist();
        auto* matched = new std::vector<std::wstring>();
        
        if (query.empty()) {
            *matched = original;
        } else {
            std::vector<std::pair<std::wstring, int>> matchedPairs;
            for (const auto& songPath : original) {
                if (currentSearch != g_SearchCounter) {
                    delete matched;
                    return; // Cancelled by newer search
                }
                fs::path p(songPath);
                std::wstring filename = p.stem().wstring();
                int score = FuzzyMatchScore(filename, query);
                if (score >= 0) {
                    matchedPairs.push_back({songPath, score});
                }
            }
            
            std::sort(matchedPairs.begin(), matchedPairs.end(), [](const auto& a, const auto& b) {
                return a.second > b.second;
            });
            
            for (const auto& m : matchedPairs) {
                matched->push_back(m.first);
            }
        }
        
        if (currentSearch == g_SearchCounter && g_GUI && g_GUI->getMainWindow()) {
            PostMessageW(g_GUI->getMainWindow(), WM_USER + 4, (WPARAM)matched, currentSearch);
        } else {
            delete matched;
        }
    }).detach();
}

// Forward Declarations
void TogglePlayPause();
void PlayNextSong();
void PlayPrevSong();
void PlaySongAtIndex(int idx);
void LoadSongAtIndexWithoutPlaying(int idx);
void ScanDirectory(const std::wstring& dir);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK TrackbarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK PlaylistListBoxSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

std::wstring GetConfigPath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        std::wstring dir = std::wstring(path) + L"\\AntigravityMusicPlayer";
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir + L"\\lastfolder.txt";
    }
    return g_AppDir + L"\\lastfolder.txt";
}

std::wstring BrowseForFolder(HWND hwndOwner) {
    wchar_t displayName[MAX_PATH] = L"";
    BROWSEINFOW bi = { 0 };
    bi.hwndOwner = hwndOwner;
    bi.pszDisplayName = displayName;
    bi.lpszTitle = L"Select Music Folder";
    bi.ulFlags = BIF_USENEWUI | BIF_NONEWFOLDERBUTTON;
    
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != NULL) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            CoTaskMemFree(pidl);
            return std::wstring(path);
        }
        CoTaskMemFree(pidl);
    }
    return L"";
}

void PlaySongAtIndex(int idx) {
    const auto& playlist = g_Shuffler.getPlaylist();
    if (playlist.empty() || idx < 0 || idx >= static_cast<int>(playlist.size())) {
        return;
    }
    
    int skipAttempts = 0;
    int currentIdx = idx;
    bool success = false;
    
    while (skipAttempts < playlist.size()) {
        std::wstring songPath = playlist[currentIdx];
        if (g_Audio.loadAndPlay(songPath)) {
            g_CurrentSongPath = songPath;
            g_Shuffler.selectSong(songPath);
            fs::path p(songPath);
            if (g_GUI) {
                g_GUI->updateSongLabel(p.stem().wstring());
                g_GUI->updatePlayPauseButton(true);
            }
            
            // Update listbox highlight to match current song in filtered results
            UpdatePlaylistHighlight();
            
            if (g_GUI) {
                g_GUI->setTrackbarRange(g_Audio.getDuration());
            }
            g_IsPlaying = true;
            success = true;
            break;
        } else {
            // If play fails, attempt to skip to the next track
            skipAttempts++;
            std::wstring nextSong = g_Shuffler.getNextSong();
            currentIdx = g_Shuffler.getSongIndexInPlaylist(nextSong);
            if (currentIdx == -1) currentIdx = 0;
        }
    }
    
    if (!success) {
        g_Audio.stop();
        if (g_GUI) {
            g_GUI->updateSongLabel(L"Failed to play files. Try another folder.");
            g_GUI->updatePlayPauseButton(false);
        }
        g_IsPlaying = false;
    }
}

void LoadSongAtIndexWithoutPlaying(int idx) {
    const auto& playlist = g_Shuffler.getPlaylist();
    if (playlist.empty() || idx < 0 || idx >= static_cast<int>(playlist.size())) {
        return;
    }
    
    std::wstring songPath = playlist[idx];
    if (g_Audio.loadWithoutPlaying(songPath)) {
        g_CurrentSongPath = songPath;
        g_Shuffler.selectSong(songPath);
        fs::path p(songPath);
        if (g_GUI) {
            g_GUI->updateSongLabel(p.stem().wstring());
            g_GUI->updatePlayPauseButton(false); // Show play icon (▶)
        }
        
        UpdatePlaylistHighlight();
        
        if (g_GUI) {
            g_GUI->setTrackbarRange(g_Audio.getDuration());
        }
        g_IsPlaying = false;
    }
}

void PlayNextSong() {
    std::wstring nextSong = g_Shuffler.getNextSong();
    if (!nextSong.empty()) {
        int idx = g_Shuffler.getSongIndexInPlaylist(nextSong);
        PlaySongAtIndex(idx);
    }
}

void PlayPrevSong() {
    std::wstring prevSong = g_Shuffler.getPreviousSong();
    if (!prevSong.empty()) {
        int idx = g_Shuffler.getSongIndexInPlaylist(prevSong);
        PlaySongAtIndex(idx);
    }
}

void TogglePlayPause() {
    if (g_Shuffler.isEmpty()) {
        return;
    }
    
    if (g_IsPlaying) {
        g_Audio.pause();
        g_GUI->updatePlayPauseButton(false);
        g_IsPlaying = false;
    } else {
        if (!g_CurrentSongPath.empty()) {
            g_Audio.resume();
            g_GUI->updatePlayPauseButton(true);
            g_IsPlaying = true;
        } else {
            PlayNextSong();
        }
    }
}

void ScanDirectory(const std::wstring& dir) {
    std::thread([dir]() {
        auto* audioFiles = new std::vector<std::wstring>();
        try {
            if (fs::exists(dir) && fs::is_directory(dir)) {
                for (const auto& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied)) {
                    if (entry.is_regular_file()) {
                        std::wstring ext = entry.path().extension().wstring();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                        if (ext == L".mp3" || ext == L".wav" || ext == L".wma" || ext == L".aac" ||
                            ext == L".m4a" || ext == L".flac" || ext == L".ogg" || ext == L".mid" ||
                            ext == L".midi" || ext == L".aiff" || ext == L".aif") {
                            audioFiles->push_back(entry.path().wstring());
                        }
                    }
                }
            }
        } catch (...) {
            // Handle read permission issues during directory crawl gracefully
        }
        
        if (g_GUI && g_GUI->getMainWindow()) {
            PostMessageW(g_GUI->getMainWindow(), WM_USER + 3, (WPARAM)audioFiles, 0);
        } else {
            delete audioFiles;
        }
    }).detach();
}



static HFONT s_hBtnFont = NULL;
static HFONT s_hBtnSearchFont = NULL;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            if (!s_hBtnFont) {
                s_hBtnFont = CreateFontW(13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                         DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            }
            if (!s_hBtnSearchFont) {
                s_hBtnSearchFont = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                               DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            }
            g_GUI = new PlayerGUI(((LPCREATESTRUCT)lParam)->hInstance, hWnd, g_AppDir);
            g_GUI->createControls();
            
            // Subclass the buttons to enable hover tracking
            SetWindowSubclass(g_GUI->getPrevButton(), ButtonSubclassProc, ID_BTN_PREV, 0);
            SetWindowSubclass(g_GUI->getPlayPauseButton(), ButtonSubclassProc, ID_BTN_PLAY_PAUSE, 0);
            SetWindowSubclass(g_GUI->getNextButton(), ButtonSubclassProc, ID_BTN_NEXT, 0);
            SetWindowSubclass(g_GUI->getShuffleButton(), ButtonSubclassProc, ID_BTN_SHUFFLE, 0);
            SetWindowSubclass(g_GUI->getPlaylistButton(), ButtonSubclassProc, ID_BTN_PLAYLIST, 0);
            SetWindowSubclass(g_GUI->getSearchButton(), ButtonSubclassProc, ID_BTN_SEARCH, 0);
            
            // Subclass trackbars to remove focus border and support custom hover expansions
            SetWindowSubclass(g_GUI->getTrackbar(), TrackbarSubclassProc, ID_TRACKBAR, 0);
            SetWindowSubclass(g_GUI->getTrackbarVolume(), TrackbarSubclassProc, ID_TRACKBAR_VOLUME, 0);
            
            // Subclass listbox to capture click-to-play on the first click
            SetWindowSubclass(g_GUI->getPlaylistListBox(), PlaylistListBoxSubclassProc, ID_LISTBOX_PLAYLIST, 0);
            
            // Set update timer for timeline status
            SetTimer(hWnd, TIMER_ID, TIMER_INTERVAL, NULL);
            
            // Deferred load of cached directory path
            PostMessageW(hWnd, WM_USER + 1, 0, 0);
            break;
        }
        
        case WM_USER + 1: {
            // Load last used folder path
            std::wstring cachePath = GetConfigPath();
            std::wifstream cacheFile(cachePath.c_str());
            if (cacheFile.is_open()) {
                std::wstring cachedDir;
                if (std::getline(cacheFile, cachedDir)) {
                    if (fs::exists(cachedDir) && fs::is_directory(cachedDir)) {
                        ScanDirectory(cachedDir);
                    }
                }
            }
            break;
        }
        case WM_USER + 2: {
            int idx = (int)wParam;
            if (idx >= 0 && idx < static_cast<int>(g_DisplayedPlaylist.size())) {
                std::wstring songPath = g_DisplayedPlaylist[idx];
                int index = g_Shuffler.getSongIndexInPlaylist(songPath);
                PlaySongAtIndex(index);
            }
            break;
        }
        case WM_USER + 3: {
            auto* audioFiles = (std::vector<std::wstring>*)wParam;
            if (audioFiles && !audioFiles->empty()) {
                g_Shuffler.loadPlaylist(*audioFiles);
                if (g_GUI && g_GUI->getSearchEdit()) {
                    SetWindowTextW(g_GUI->getSearchEdit(), L"");
                }
                FilterPlaylist(L"");
                LoadSongAtIndexWithoutPlaying(0);
            } else if (audioFiles) {
                MessageBoxW(g_GUI->getMainWindow(), L"No supported audio files found in selected directory.", L"No Music Found", MB_OK | MB_ICONWARNING);
            }
            delete audioFiles;
            break;
        }
        case WM_USER + 4: {
            auto* matched = (std::vector<std::wstring>*)wParam;
            int searchId = (int)lParam;
            if (searchId == g_SearchCounter && matched) {
                g_DisplayedPlaylist = *matched;
                g_GUI->populatePlaylist(g_DisplayedPlaylist);
                UpdatePlaylistHighlight();
            }
            delete matched;
            break;
        }
        case WM_TIMER: {
            if (wParam == TIMER_ID) {
                if (g_IsPlaying) {
                    // Native tracks auto-advancing check
                    g_Audio.updateStatus();
                    if (!g_Audio.isPlaying()) {
                        PlayNextSong();
                    } else if (!IsIconic(hWnd) && !g_IsDraggingTrackbar) {
                        // Only fetch position and redraw GUI if the window is actually visible
                        int pos = g_Audio.getPosition();
                        int dur = g_Audio.getDuration();
                        g_GUI->updateTimeLabels(pos, dur);
                        SendMessageW(g_GUI->getTrackbar(), TBM_SETPOS, TRUE, pos);
                        InvalidateRect(g_GUI->getTrackbar(), NULL, FALSE);
                    }
                }
            }
            break;
        }
        
        case WM_HSCROLL: {
            HWND hSlider = (HWND)lParam;
            if (hSlider == g_GUI->getTrackbar()) {
                int code = LOWORD(wParam);
                int pos = SendMessageW(hSlider, TBM_GETPOS, 0, 0);
                int dur = g_Audio.getDuration();
                
                if (code == TB_THUMBTRACK) {
                    g_IsDraggingTrackbar = true;
                    g_GUI->updateTimeLabels(pos, dur);
                    InvalidateRect(hSlider, NULL, FALSE);
                } else if (code == TB_THUMBPOSITION || code == TB_ENDTRACK) {
                    g_IsDraggingTrackbar = false;
                    g_Audio.setPosition(pos);
                    InvalidateRect(hSlider, NULL, FALSE);
                } else {
                    g_IsDraggingTrackbar = false;
                    g_Audio.setPosition(pos);
                    InvalidateRect(hSlider, NULL, FALSE);
                }
            } else if (hSlider == g_GUI->getTrackbarVolume()) {
                int pos = SendMessageW(hSlider, TBM_GETPOS, 0, 0);
                g_Audio.setVolume(pos);
                
                wchar_t buf[16];
                swprintf_s(buf, L"%d%%", pos);
                SetWindowTextW(g_GUI->getVolumeLabel(), buf);
                InvalidateRect(hSlider, NULL, FALSE);
            }
            break;
        }
        
        case WM_NOTIFY: {
            LPNMHDR pnmhdr = (LPNMHDR)lParam;
            if ((pnmhdr->idFrom == ID_TRACKBAR || pnmhdr->idFrom == ID_TRACKBAR_VOLUME) && pnmhdr->code == NM_CUSTOMDRAW) {
                LPNMCUSTOMDRAW lpnmcd = (LPNMCUSTOMDRAW)lParam;
                if (lpnmcd->dwDrawStage == CDDS_PREPAINT) {
                    HDC hdc = lpnmcd->hdc;
                    HWND hTrack = lpnmcd->hdr.hwndFrom;
                    
                    // Get the full client rect of the trackbar control
                    RECT rcClient;
                    GetClientRect(hTrack, &rcClient);
                    
                    // Clear the entire trackbar background
                    HBRUSH hBg = g_GUI->getDarkBgBrush();
                    FillRect(hdc, &rcClient, hBg);
                    
                    // Get channel rect and thumb rect from the control
                    RECT rcChannel;
                    SendMessageW(hTrack, TBM_GETCHANNELRECT, 0, (LPARAM)&rcChannel);
                    RECT rcThumb;
                    SendMessageW(hTrack, TBM_GETTHUMBRECT, 0, (LPARAM)&rcThumb);
                    
                    // Check if mouse is hovering over this trackbar
                    bool isHovered = (GetPropW(hTrack, L"TrackbarHovered") != NULL);
                    int trackHalfHeight = isHovered ? 3 : 2;  // Thicker (6px) on hover, else (4px)
                    int thumbRadius = isHovered ? 9 : 7;      // Larger (18px diameter) on hover, else (14px)
                    
                    int thumbCenterX = rcThumb.left + (rcThumb.right - rcThumb.left) / 2;
                    int cy = rcChannel.top + (rcChannel.bottom - rcChannel.top) / 2;
                    
                    // Played portion color (timeline = accent purple, volume = light blue)
                    COLORREF playedColor = (pnmhdr->idFrom == ID_TRACKBAR_VOLUME) ? RGB(100, 180, 255) : CLR_ACCENT;
                    
                    // Played portion
                    RECT rcPlayed = { rcChannel.left, cy - trackHalfHeight, thumbCenterX, cy + trackHalfHeight };
                    HBRUSH hPlayedBrush = CreateSolidBrush(playedColor);
                    FillRect(hdc, &rcPlayed, hPlayedBrush);
                    DeleteObject(hPlayedBrush);
                    
                    // Unplayed portion (dark slate grey)
                    RECT rcUnplayed = { thumbCenterX, cy - trackHalfHeight, rcChannel.right, cy + trackHalfHeight };
                    HBRUSH hUnplayedBrush = CreateSolidBrush(RGB(48, 48, 64));
                    FillRect(hdc, &rcUnplayed, hUnplayedBrush);
                    DeleteObject(hUnplayedBrush);
                    
                    // Draw the thumb circle on top of the channel
                    int thumbCX = thumbCenterX;
                    int thumbCY = rcThumb.top + (rcThumb.bottom - rcThumb.top) / 2;
                    
                    HBRUSH hThumbBrush = CreateSolidBrush(RGB(255, 255, 255));
                    HPEN hThumbPen = CreatePen(PS_SOLID, 2, playedColor);
                    
                    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hThumbBrush);
                    HPEN hOldPen = (HPEN)SelectObject(hdc, hThumbPen);
                    
                    Ellipse(hdc, thumbCX - thumbRadius, thumbCY - thumbRadius, thumbCX + thumbRadius, thumbCY + thumbRadius);
                    
                    SelectObject(hdc, hOldBrush);
                    SelectObject(hdc, hOldPen);
                    DeleteObject(hThumbBrush);
                    DeleteObject(hThumbPen);
                    
                    return CDRF_SKIPDEFAULT;
                }
            }
            break;
        }
        
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);
            
            switch (wmId) {
                case ID_BTN_MENU_FILE: {
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, ID_MENU_SELECT_FOLDER, L"Select Music Folder...");
                    AppendMenuW(hMenu, MF_STRING, ID_MENU_RECENT_FOLDER, L"Open Recent Folder");
                    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenuW(hMenu, MF_STRING, ID_MENU_EXIT, L"Exit");
                    
                    RECT rc;
                    GetWindowRect((HWND)lParam, &rc);
                    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, hWnd, NULL);
                    DestroyMenu(hMenu);
                    break;
                }
                
                case ID_BTN_MENU_VIEW: {
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, ID_MENU_PLAYLIST, L"Toggle Playlist Sidebar");
                    
                    RECT rc;
                    GetWindowRect((HWND)lParam, &rc);
                    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, hWnd, NULL);
                    DestroyMenu(hMenu);
                    break;
                }
                
                case ID_BTN_MENU_WINDOW: {
                    HMENU hMenu = CreatePopupMenu();
                    AppendMenuW(hMenu, MF_STRING, ID_MENU_ABOUT, L"About Antigravity Player");
                    
                    RECT rc;
                    GetWindowRect((HWND)lParam, &rc);
                    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, hWnd, NULL);
                    DestroyMenu(hMenu);
                    break;
                }
                
                case ID_MENU_SELECT_FOLDER: {
                    std::wstring path = BrowseForFolder(hWnd);
                    if (!path.empty()) {
                        ScanDirectory(path);
                        
                        // Cache the folder path
                        std::wstring cachePath = GetConfigPath();
                        std::wofstream cacheFile(cachePath.c_str());
                        if (cacheFile.is_open()) {
                            cacheFile << path;
                        }
                    }
                    break;
                }
                
                case ID_MENU_RECENT_FOLDER: {
                    std::wstring cachePath = GetConfigPath();
                    std::wifstream cacheFile(cachePath.c_str());
                    if (cacheFile.is_open()) {
                        std::wstring cachedDir;
                        if (std::getline(cacheFile, cachedDir)) {
                            if (fs::exists(cachedDir) && fs::is_directory(cachedDir)) {
                                ScanDirectory(cachedDir);
                            } else {
                                MessageBoxW(hWnd, L"Recent folder no longer exists.", L"Folder Error", MB_OK | MB_ICONWARNING);
                            }
                        }
                    } else {
                        MessageBoxW(hWnd, L"No cached folder folder found.", L"Folder Error", MB_OK | MB_ICONWARNING);
                    }
                    break;
                }
                
                case ID_MENU_EXIT:
                    DestroyWindow(hWnd);
                    break;
                    
                case ID_MENU_PLAYLIST:
                case ID_BTN_PLAYLIST:
                    g_GUI->togglePlaylist();
                    break;
                    
                case ID_MENU_ABOUT:
                    MessageBoxW(hWnd, 
                        L"Antigravity Music Player\n\n"
                        L"A sleek, lightweight custom C++ Win32 audio player.\n\n"
                        L"Features:\n"
                        L"• True Entropy Fisher-Yates Shuffle\n"
                        L"• Multi-Format Audio Playback\n"
                        L"• Dynamic Premium Dark Theme\n"
                        L"• Owner-Drawn Custom Menus & Playlists\n"
                        L"• Low Memory Caching of Audio Duration\n"
                        L"• Remember/Resume Last Music Directory\n"
                        L"• Global Bluetooth / Keyboard Media Keys Support\n\n"
                        L"Designed by Antigravity.", 
                        L"About Antigravity Player", 
                        MB_OK | MB_ICONINFORMATION);
                    break;
                    
                case ID_BTN_PLAY_PAUSE:
                    TogglePlayPause();
                    break;
                    
                case ID_BTN_PREV:
                    PlayPrevSong();
                    break;
                    
                case ID_BTN_NEXT:
                    PlayNextSong();
                    break;
                    
                case ID_BTN_SHUFFLE:
                    g_Shuffler.setShuffleEnabled(!g_Shuffler.isShuffleEnabled());
                    g_GUI->updateShuffleButton(g_Shuffler.isShuffleEnabled());
                    break;
                    
                case ID_EDIT_SEARCH: {
                    if (wmEvent == EN_CHANGE) {
                        wchar_t query[256] = L"";
                        GetWindowTextW(g_GUI->getSearchEdit(), query, 256);
                        FilterPlaylist(query);
                        
                        // Toggle search icon to clear icon (✕) if search query is entered
                        if (wcslen(query) > 0) {
                            SetWindowTextW(g_GUI->getSearchButton(), L"✕");
                        } else {
                            SetWindowTextW(g_GUI->getSearchButton(), L"🔍");
                        }
                        InvalidateRect(g_GUI->getSearchButton(), NULL, FALSE);
                    }
                    break;
                }
                
                case ID_BTN_SEARCH: {
                    // Click search button clears the search query and restores full playlist
                    SetWindowTextW(g_GUI->getSearchEdit(), L"");
                    FilterPlaylist(L"");
                    SetFocus(g_GUI->getSearchEdit());
                    break;
                }
                    
                case ID_LISTBOX_PLAYLIST: {
                    if (wmEvent == LBN_SELCHANGE || wmEvent == LBN_DBLCLK) {
                        int idx = SendMessageW(g_GUI->getPlaylistListBox(), LB_GETCURSEL, 0, 0);
                        if (idx != LB_ERR && idx < static_cast<int>(g_DisplayedPlaylist.size())) {
                            std::wstring songPath = g_DisplayedPlaylist[idx];
                            int index = g_Shuffler.getSongIndexInPlaylist(songPath);
                            PlaySongAtIndex(index);
                        }
                    }
                    break;
                }
            }
            break;
        }
        
        case WM_ACTIVATE: {
            // Trim memory ONLY when minimized, not on inactive, to avoid thrashing
            break;
        }
        
        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) {
                // Trim memory working set to the absolute minimum when minimized
                SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            SetBkMode(hdc, TRANSPARENT);
            
            // Check if it is the current time or total duration text box
            LONG_PTR id = GetWindowLongPtrW(hCtrl, GWLP_ID);
            if (id == ID_LABEL_CUR_TIME || id == ID_LABEL_TOT_TIME || id == ID_LABEL_VOLUME) {
                SetTextColor(hdc, CLR_TEXT_SECONDARY);
            } else {
                SetTextColor(hdc, CLR_TEXT_PRIMARY);
            }
            return g_GUI ? (INT_PTR)g_GUI->getDarkBgBrush() : (INT_PTR)GetStockObject(BLACK_BRUSH);
        }
        
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, CLR_TEXT_PRIMARY);
            return g_GUI ? (INT_PTR)g_GUI->getDarkListBrush() : (INT_PTR)GetStockObject(BLACK_BRUSH);
        }
        
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, CLR_TEXT_PRIMARY);
            SetBkColor(hdc, CLR_DARK_CTRL);
            return g_GUI ? (INT_PTR)g_GUI->getDarkCtrlBrush() : (INT_PTR)GetStockObject(BLACK_BRUSH);
        }
        
        case WM_CTLCOLORBTN: {
            return g_GUI ? (INT_PTR)g_GUI->getDarkCtrlBrush() : (INT_PTR)GetStockObject(BLACK_BRUSH);
        }
        
        case WM_ERASEBKGND: {
            if (g_GUI) {
                HDC hdc = (HDC)wParam;
                RECT rc;
                GetClientRect(hWnd, &rc);
                FillRect(hdc, &rc, g_GUI->getDarkBgBrush());
                return 1;
            }
            return 0;
        }
        
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            
            // Draw a subtle divider line separating the menu bar at the top (y = 35)
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(48, 48, 64));
            HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
            
            MoveToEx(hdc, 0, 35, NULL);
            LineTo(hdc, 800, 35);
            
            SelectObject(hdc, hOldPen);
            DeleteObject(hPen);
            
            EndPaint(hWnd, &ps);
            break;
        }
        
        case WM_MEASUREITEM: {
            MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
            if (mis->CtlID == ID_LISTBOX_PLAYLIST) {
                mis->itemHeight = 40;
            }
            return TRUE;
        }
        
        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlID == ID_BTN_MENU_FILE || dis->CtlID == ID_BTN_MENU_VIEW || dis->CtlID == ID_BTN_MENU_WINDOW) {
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                
                // Draw menu button backgrounds (flat dark)
                HBRUSH hBg = NULL;
                bool isTempBrush = false;
                if (g_GUI) {
                    hBg = g_GUI->getDarkBgBrush();
                } else {
                    hBg = CreateSolidBrush(CLR_DARK_BG);
                    isTempBrush = true;
                }
                if (dis->itemState & ODS_SELECTED) {
                    if (isTempBrush) DeleteObject(hBg);
                    hBg = CreateSolidBrush(RGB(36, 36, 48));
                    isTempBrush = true;
                }
                FillRect(hdc, &rc, hBg);
                if (isTempBrush && hBg) {
                    DeleteObject(hBg);
                }
                
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(80, 160, 240)); // Premium Light Blue Text for Menus
                
                wchar_t text[32];
                GetWindowTextW(dis->hwndItem, text, 32);
                DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                return TRUE;
            }
            
            // Standard control buttons
            if (dis->CtlType == ODT_BUTTON) {
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                
                bool isSelected = (dis->itemState & ODS_SELECTED);
                bool isHovered = (GetPropW(dis->hwndItem, L"HoverTracked") != NULL);
                
                // Check if button is Shuffle or Playlist, which have active toggle states
                bool isActiveToggle = false;
                if (dis->CtlID == ID_BTN_SHUFFLE) {
                    isActiveToggle = g_Shuffler.isShuffleEnabled();
                } else if (dis->CtlID == ID_BTN_PLAYLIST && g_GUI) {
                    isActiveToggle = g_GUI->isPlaylistVisible();
                }
                
                COLORREF colorBgNormal = RGB(36, 36, 48);
                COLORREF colorBgHover = RGB(50, 50, 68);
                COLORREF colorBgActive = RGB(120, 90, 255);
                COLORREF colorBorderNormal = RGB(64, 64, 80);
                COLORREF colorBorderHover = RGB(100, 80, 220);
                COLORREF colorBorderActive = RGB(160, 140, 255);
                
                COLORREF colorBg = colorBgNormal;
                COLORREF colorBorder = colorBorderNormal;
                COLORREF colorText = RGB(230, 230, 240);
                
                if (isActiveToggle || isSelected) {
                    colorBg = colorBgActive;
                    colorBorder = colorBorderActive;
                    colorText = RGB(255, 255, 255);
                } else if (isHovered) {
                    colorBg = colorBgHover;
                    colorBorder = colorBorderHover;
                }
                
                HPEN hPen = CreatePen(PS_SOLID, 1, colorBorder);
                HBRUSH hBrush = CreateSolidBrush(colorBg);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
                HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
                
                bool isCircle = (dis->CtlID == ID_BTN_PREV || dis->CtlID == ID_BTN_PLAY_PAUSE || dis->CtlID == ID_BTN_NEXT);
                
                if (isCircle) {
                    Ellipse(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1);
                } else {
                    int r = (dis->CtlID == ID_BTN_SEARCH) ? 10 : 18;
                    RoundRect(hdc, rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1, r, r);
                }
                
                // Draw symbols / text
                HBRUSH hIconBrush = CreateSolidBrush(colorText);
                HPEN hIconPen = CreatePen(PS_SOLID, 1, colorText);
                HPEN hOldIconPen = (HPEN)SelectObject(hdc, hIconPen);
                HBRUSH hOldIconBrush = (HBRUSH)SelectObject(hdc, hIconBrush);
                
                if (dis->CtlID == ID_BTN_PLAY_PAUSE) {
                    wchar_t btnText[16];
                    GetWindowTextW(dis->hwndItem, btnText, 16);
                    bool drawPause = (wcscmp(btnText, L"⏸") == 0);
                    
                    int cx = rc.left + (rc.right - rc.left) / 2;
                    int cy = rc.top + (rc.bottom - rc.top) / 2;
                    
                    if (drawPause) {
                        RECT rcLeft = { cx - 6, cy - 10, cx - 2, cy + 10 };
                        RECT rcRight = { cx + 2, cy - 10, cx + 6, cy + 10 };
                        FillRect(hdc, &rcLeft, hIconBrush);
                        FillRect(hdc, &rcRight, hIconBrush);
                    } else {
                        POINT pts[3];
                        pts[0].x = cx - 5;  pts[0].y = cy - 11;
                        pts[1].x = cx - 5;  pts[1].y = cy + 11;
                        pts[2].x = cx + 8;  pts[2].y = cy;
                        Polygon(hdc, pts, 3);
                    }
                }
                else if (dis->CtlID == ID_BTN_PREV) {
                    int cx = rc.left + (rc.right - rc.left) / 2;
                    int cy = rc.top + (rc.bottom - rc.top) / 2;
                    
                    POINT pts1[3];
                    pts1[0].x = cx + 6;  pts1[0].y = cy - 8;
                    pts1[1].x = cx + 6;  pts1[1].y = cy + 8;
                    pts1[2].x = cx - 2;  pts1[2].y = cy;
                    Polygon(hdc, pts1, 3);
                    
                    POINT pts2[3];
                    pts2[0].x = cx - 2;   pts2[0].y = cy - 8;
                    pts2[1].x = cx - 2;   pts2[1].y = cy + 8;
                    pts2[2].x = cx - 10;  pts2[2].y = cy;
                    Polygon(hdc, pts2, 3);
                    
                    RECT rcBar = { cx - 12, cy - 8, cx - 10, cy + 8 };
                    FillRect(hdc, &rcBar, hIconBrush);
                }
                else if (dis->CtlID == ID_BTN_NEXT) {
                    int cx = rc.left + (rc.right - rc.left) / 2;
                    int cy = rc.top + (rc.bottom - rc.top) / 2;
                    
                    POINT pts1[3];
                    pts1[0].x = cx - 6;  pts1[0].y = cy - 8;
                    pts1[1].x = cx - 6;  pts1[1].y = cy + 8;
                    pts1[2].x = cx + 2;  pts1[2].y = cy;
                    Polygon(hdc, pts1, 3);
                    
                    POINT pts2[3];
                    pts2[0].x = cx + 2;   pts2[0].y = cy - 8;
                    pts2[1].x = cx + 2;   pts2[1].y = cy + 8;
                    pts2[2].x = cx + 10;  pts2[2].y = cy;
                    Polygon(hdc, pts2, 3);
                    
                    RECT rcBar = { cx + 10, cy - 8, cx + 12, cy + 8 };
                    FillRect(hdc, &rcBar, hIconBrush);
                }
                else {
                    wchar_t btnText[64];
                    GetWindowTextW(dis->hwndItem, btnText, 64);
                    
                    HFONT hFontBtn = (dis->CtlID == ID_BTN_SEARCH) ? s_hBtnSearchFont : s_hBtnFont;
                    HFONT hOldFont = (HFONT)SelectObject(hdc, hFontBtn);
                    
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, colorText);
                    
                    DrawTextW(hdc, btnText, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    
                    SelectObject(hdc, hOldFont);
                }
                
                // Cleanup icon drawing resources
                SelectObject(hdc, hOldIconPen);
                SelectObject(hdc, hOldIconBrush);
                DeleteObject(hIconPen);
                DeleteObject(hIconBrush);
                
                // Cleanup background drawing resources
                SelectObject(hdc, hOldPen);
                SelectObject(hdc, hOldBrush);
                DeleteObject(hPen);
                DeleteObject(hBrush);
                
                return TRUE;
            }
            
            // Playlist item owner draw (vinyl disk + stem filename)
            if (dis->CtlID == ID_LISTBOX_PLAYLIST && dis->itemID != -1) {
                HDC hdc = dis->hDC;
                RECT rc = dis->rcItem;
                
                bool selected = (dis->itemState & ODS_SELECTED);
                HBRUSH hBg = NULL;
                bool isTempBrush = false;
                if (selected) {
                    hBg = CreateSolidBrush(RGB(48, 48, 64));
                    isTempBrush = true;
                } else if (g_GUI) {
                    hBg = g_GUI->getDarkListBrush();
                } else {
                    hBg = CreateSolidBrush(CLR_DARK_LIST);
                    isTempBrush = true;
                }
                FillRect(hdc, &rc, hBg);
                if (isTempBrush && hBg) DeleteObject(hBg);
                
                // Get item text
                wchar_t text[MAX_PATH];
                SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);
                
                // Draw Vinyl disc icon (24px colored circle with dark inner hole)
                int iconSize = 24;
                int yOffset = (rc.bottom - rc.top - iconSize) / 2;
                int xOffset = 10;
                
                RECT circleRc = { rc.left + xOffset, rc.top + yOffset, rc.left + xOffset + iconSize, rc.top + yOffset + iconSize };
                
                // 4 accent colors cycle: purple, pink, teal, amber
                COLORREF iconColors[] = { RGB(120, 90, 255), RGB(255, 105, 180), RGB(0, 128, 128), RGB(255, 191, 0) };
                COLORREF iconColor = iconColors[dis->itemID % 4];
                
                HBRUSH hIconBrush = CreateSolidBrush(iconColor);
                HPEN hIconPen = CreatePen(PS_SOLID, 1, RGB(16, 16, 24));
                HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hIconBrush);
                HPEN hOldPen = (HPEN)SelectObject(hdc, hIconPen);
                
                // Draw colored outer circle
                Ellipse(hdc, circleRc.left, circleRc.top, circleRc.right, circleRc.bottom);
                
                // Draw black inner hole
                int cx = circleRc.left + iconSize / 2;
                int cy = circleRc.top + iconSize / 2;
                int r_hole = 3;
                HBRUSH hHoleBrush = CreateSolidBrush(RGB(16, 16, 24));
                SelectObject(hdc, hHoleBrush);
                Ellipse(hdc, cx - r_hole, cy - r_hole, cx + r_hole, cy + r_hole);
                
                SelectObject(hdc, hOldBrush);
                SelectObject(hdc, hOldPen);
                DeleteObject(hIconBrush);
                DeleteObject(hIconPen);
                DeleteObject(hHoleBrush);
                
                // Draw stem name text next to vinyl disc icon
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, selected ? RGB(255, 255, 255) : CLR_TEXT_PRIMARY);
                
                RECT textRc = { rc.left + xOffset + iconSize + 10, rc.top, rc.right - 10, rc.bottom };
                DrawTextW(hdc, text, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                return TRUE;
            }
            break;
        }
        
        case WM_APPCOMMAND: {
            int cmd = GET_APPCOMMAND_LPARAM(lParam);
            switch (cmd) {
                case APPCOMMAND_MEDIA_PLAY:
                    if (!g_IsPlaying) TogglePlayPause();
                    return TRUE;
                case APPCOMMAND_MEDIA_PAUSE:
                    if (g_IsPlaying) TogglePlayPause();
                    return TRUE;
                case APPCOMMAND_MEDIA_PLAY_PAUSE:
                    TogglePlayPause();
                    return TRUE;
                case APPCOMMAND_MEDIA_NEXTTRACK:
                    PlayNextSong();
                    return TRUE;
                case APPCOMMAND_MEDIA_PREVIOUSTRACK:
                    PlayPrevSong();
                    return TRUE;
                case APPCOMMAND_MEDIA_STOP:
                    g_Audio.stop();
                    g_GUI->updatePlayPauseButton(false);
                    g_IsPlaying = false;
                    return TRUE;
            }
            break;
        }
        
        case WM_DESTROY: {
            if (s_hBtnFont) {
                DeleteObject(s_hBtnFont);
                s_hBtnFont = NULL;
            }
            if (s_hBtnSearchFont) {
                DeleteObject(s_hBtnSearchFont);
                s_hBtnSearchFont = NULL;
            }
            
            KillTimer(hWnd, TIMER_ID);
            delete g_GUI;
            PostQuitMessage(0);
            break;
        }
        
        default:
            return DefWindowProcW(hWnd, message, wParam, lParam);
    }
    return 0;
}

LRESULT CALLBACK TrackbarSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_SETFOCUS: {
            // Intercept keyboard focus to completely prevent the focus rectangle outline from drawing
            SetFocus(GetParent(hWnd));
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (!GetPropW(hWnd, L"TrackbarHovered")) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hWnd;
                TrackMouseEvent(&tme);
                SetPropW(hWnd, L"TrackbarHovered", (HANDLE)1);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            RemovePropW(hWnd, L"TrackbarHovered");
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }
        case WM_DESTROY: {
            RemovePropW(hWnd, L"TrackbarHovered");
            break;
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK PlaylistListBoxSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_LBUTTONUP) {
        // Let listbox update selection state
        LRESULT res = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        
        // Use LB_ITEMFROMPOINT to verify the click is actually on an item,
        // not the scrollbar or background.
        DWORD itemInfo = (DWORD)SendMessageW(hWnd, LB_ITEMFROMPOINT, 0, lParam);
        int isOutside = HIWORD(itemInfo);
        int idx = LOWORD(itemInfo);
        
        if (isOutside == 0 && idx != -1) {
            PostMessageW(GetParent(hWnd), WM_USER + 2, idx, 0);
        }
        return res;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK ButtonSubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_MOUSEMOVE: {
            if (!GetPropW(hWnd, L"HoverTracked")) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hWnd;
                TrackMouseEvent(&tme);
                SetPropW(hWnd, L"HoverTracked", (HANDLE)1);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            break;
        }
        case WM_MOUSELEAVE: {
            RemovePropW(hWnd, L"HoverTracked");
            InvalidateRect(hWnd, NULL, FALSE);
            break;
        }
        case WM_DESTROY: {
            RemovePropW(hWnd, L"HoverTracked");
            break;
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
    // Set up application directory path
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    fs::path p(exePath);
    g_AppDir = p.parent_path().wstring();
    
    INITCOMMONCONTROLSEX icex = { 0 };
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);
    
    const wchar_t CLASS_NAME[] = L"LightweightMusicPlayerClass";
    WNDCLASSW wc = { };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = NULL; // Handled in WM_ERASEBKGND
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(101));
    
    RegisterClassW(&wc);
    
    // Default window width (Playlist sidebar hidden)
    HWND hWnd = CreateWindowExW(
        0, CLASS_NAME, L"Antigravity Music Player",
        WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME ^ WS_MAXIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL
    );
    
    if (hWnd == NULL) {
        CoUninitialize();
        return 0;
    }
    
    
    ShowWindow(hWnd, nCmdShow);
    
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    CoUninitialize();
    return (int)msg.wParam;
}