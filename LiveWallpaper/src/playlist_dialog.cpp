#include "playlist_dialog.h"
#include "utils.h"
#include <commctrl.h>
#include <shobjidl.h>

#define IDC_LISTBOX 101
#define IDC_ADD 102
#define IDC_REMOVE 103
#define IDC_MOVEUP 104
#define IDC_MOVEDOWN 105
#define IDC_SAVE 106
#define IDC_CANCEL 107

PlaylistDialog::PlaylistDialog() {}

PlaylistDialog::~PlaylistDialog() {
    if (m_hWnd && IsWindow(m_hWnd)) {
        DestroyWindow(m_hWnd);
    }
}

bool PlaylistDialog::Initialize(HINSTANCE hInstance) {
    m_hInstance = hInstance;

    WNDCLASSEXW wcx = { 0 };
    wcx.cbSize = sizeof(wcx);
    wcx.lpfnWndProc = WndProc;
    wcx.hInstance = hInstance;
    wcx.lpszClassName = L"LiveWallpaperPlaylistDialogClass";
    wcx.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcx.hbrBackground = (HBRUSH)(COLOR_WINDOW);

    RegisterClassExW(&wcx);
    return true;
}

void PlaylistDialog::Show(HWND parentWindow, const std::vector<std::wstring>& currentPlaylist, size_t currentIndex) {
    m_playlist = currentPlaylist;
    m_currentIndex = currentIndex;
    m_originalPlaylist = currentPlaylist;
    m_originalIndex = currentIndex;

    if (!m_hWnd) {
        m_hWnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            L"LiveWallpaperPlaylistDialogClass",
            L"Manage Playlist",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 520, 450,
            parentWindow, NULL, m_hInstance, this
        );

        if (!m_hWnd) {
            LOG_ERROR("Failed to create PlaylistDialog window.");
            return;
        }
    }

    RefreshListBox();
    ShowWindow(m_hWnd, SW_SHOW);
    SetForegroundWindow(m_hWnd);
}

void PlaylistDialog::Hide() {
    if (m_hWnd && IsWindow(m_hWnd)) {
        ShowWindow(m_hWnd, SW_HIDE);
    }
}

void PlaylistDialog::SetOnPlaylistUpdatedCallback(std::function<void(const std::vector<std::wstring>&, size_t)> cb) {
    m_onPlaylistUpdated = cb;
}

LRESULT CALLBACK PlaylistDialog::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    PlaylistDialog* pThis = nullptr;

    if (message == WM_NCCREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pThis = reinterpret_cast<PlaylistDialog*>(pCreate->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
        pThis->m_hWnd = hWnd;
    } else {
        pThis = reinterpret_cast<PlaylistDialog*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    if (pThis) {
        switch (message) {
            case WM_CREATE:
                pThis->OnInitDialog();
                return 0;
                
            case WM_COMMAND:
                if (LOWORD(wParam) == IDC_ADD) {
                    pThis->OnAddVideo();
                } else if (LOWORD(wParam) == IDC_REMOVE) {
                    pThis->OnRemoveVideo();
                } else if (LOWORD(wParam) == IDC_MOVEUP) {
                    pThis->OnMoveUp();
                } else if (LOWORD(wParam) == IDC_MOVEDOWN) {
                    pThis->OnMoveDown();
                } else if (LOWORD(wParam) == IDC_SAVE) {
                    pThis->OnSave();
                } else if (LOWORD(wParam) == IDC_CANCEL) {
                    pThis->OnCancel();
                } else if (LOWORD(wParam) == IDC_LISTBOX) {
                    if (HIWORD(wParam) == LBN_DBLCLK) {
                        HWND hListBox = GetDlgItem(hWnd, IDC_LISTBOX);
                        LRESULT sel = SendMessageW(hListBox, LB_GETCURSEL, 0, 0);
                        if (sel != LB_ERR && sel < (LRESULT)pThis->m_playlist.size()) {
                            pThis->m_currentIndex = sel;
                            pThis->RefreshListBox();
                        }
                    }
                }
                return 0;

            case WM_CLOSE:
                pThis->OnCancel();
                return 0;

            case WM_DESTROY:
                if (pThis->m_hFont) {
                    DeleteObject(pThis->m_hFont);
                    pThis->m_hFont = nullptr;
                }
                if (pThis->m_hInstance) {
                    UnregisterClassW(L"LiveWallpaperPlaylistDialogClass", pThis->m_hInstance);
                }
                return 0;
        }
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

void PlaylistDialog::OnInitDialog() {
    // Create ListBox (taller, left-aligned)
    CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        10, 10, 380, 340, m_hWnd, (HMENU)IDC_LISTBOX, m_hInstance, NULL);

    // Create Move Up Button
    CreateWindowExW(0, L"BUTTON", L"Move Up",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        400, 10, 95, 30, m_hWnd, (HMENU)IDC_MOVEUP, m_hInstance, NULL);

    // Create Move Down Button
    CreateWindowExW(0, L"BUTTON", L"Move Down",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        400, 50, 95, 30, m_hWnd, (HMENU)IDC_MOVEDOWN, m_hInstance, NULL);

    // Create Remove Button
    CreateWindowExW(0, L"BUTTON", L"Remove",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        400, 90, 95, 30, m_hWnd, (HMENU)IDC_REMOVE, m_hInstance, NULL);

    // Create Add Button
    CreateWindowExW(0, L"BUTTON", L"Add Video...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 365, 120, 30, m_hWnd, (HMENU)IDC_ADD, m_hInstance, NULL);

    // Create Save Button
    CreateWindowExW(0, L"BUTTON", L"Save",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        270, 365, 100, 30, m_hWnd, (HMENU)IDC_SAVE, m_hInstance, NULL);

    // Create Cancel Button
    CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        380, 365, 115, 30, m_hWnd, (HMENU)IDC_CANCEL, m_hInstance, NULL);

    // Set a modern font
    NONCLIENTMETRICS metrics = { sizeof(NONCLIENTMETRICS) };
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &metrics, 0);
    m_hFont = CreateFontIndirect(&metrics.lfMessageFont);
    
    SendDlgItemMessage(m_hWnd, IDC_LISTBOX, WM_SETFONT, (WPARAM)m_hFont, MAKELPARAM(TRUE, 0));
    SendDlgItemMessage(m_hWnd, IDC_MOVEUP, WM_SETFONT, (WPARAM)m_hFont, MAKELPARAM(TRUE, 0));
    SendDlgItemMessage(m_hWnd, IDC_MOVEDOWN, WM_SETFONT, (WPARAM)m_hFont, MAKELPARAM(TRUE, 0));
    SendDlgItemMessage(m_hWnd, IDC_REMOVE, WM_SETFONT, (WPARAM)m_hFont, MAKELPARAM(TRUE, 0));
    SendDlgItemMessage(m_hWnd, IDC_ADD, WM_SETFONT, (WPARAM)m_hFont, MAKELPARAM(TRUE, 0));
    SendDlgItemMessage(m_hWnd, IDC_SAVE, WM_SETFONT, (WPARAM)m_hFont, MAKELPARAM(TRUE, 0));
    SendDlgItemMessage(m_hWnd, IDC_CANCEL, WM_SETFONT, (WPARAM)m_hFont, MAKELPARAM(TRUE, 0));
}

void PlaylistDialog::RefreshListBox() {
    if (!m_hWnd) return;
    HWND hListBox = GetDlgItem(m_hWnd, IDC_LISTBOX);
    SendMessageW(hListBox, LB_RESETCONTENT, 0, 0);

    for (const auto& item : m_playlist) {
        size_t lastSlash = item.find_last_of(L"\\/");
        std::wstring display = (lastSlash != std::wstring::npos) ? item.substr(lastSlash + 1) : item;
        SendMessageW(hListBox, LB_ADDSTRING, 0, (LPARAM)display.c_str());
    }
    
    if (m_currentIndex < m_playlist.size()) {
        SendMessageW(hListBox, LB_SETCURSEL, m_currentIndex, 0);
    }
}

void PlaylistDialog::OnAddVideo() {
    IFileOpenDialog* pFileOpen = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
    
    if (SUCCEEDED(hr)) {
        COMDLG_FILTERSPEC rgSpec[] = {
            { L"Video Files", L"*.mp4;*.wmv;*.avi;*.mkv" },
            { L"All Files", L"*.*" }
        };
        pFileOpen->SetFileTypes(2, rgSpec);
        
        hr = pFileOpen->Show(m_hWnd);
        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr)) {
                PWSTR pszFilePath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                if (SUCCEEDED(hr)) {
                    std::wstring selectedPath(pszFilePath);
                    CoTaskMemFree(pszFilePath);
                    
                    if (Utils::ValidateFilePath(selectedPath, false)) {
                        m_playlist.push_back(selectedPath);
                        RefreshListBox();
                    } else {
                        MessageBoxW(m_hWnd, L"The selected file is invalid, unsafe, or has an unsupported extension.", L"Invalid File", MB_OK | MB_ICONERROR);
                    }
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    }
}

void PlaylistDialog::OnRemoveVideo() {
    if (m_playlist.empty()) return;
    
    HWND hListBox = GetDlgItem(m_hWnd, IDC_LISTBOX);
    LRESULT sel = SendMessageW(hListBox, LB_GETCURSEL, 0, 0);
    
    if (sel != LB_ERR && sel < (LRESULT)m_playlist.size()) {
        m_playlist.erase(m_playlist.begin() + sel);
        
        if (sel < m_currentIndex) {
            m_currentIndex--;
        } else if (sel == m_currentIndex) {
            if (m_currentIndex >= m_playlist.size() && !m_playlist.empty()) {
                m_currentIndex = 0;
            }
        }
        
        RefreshListBox();
    }
}

void PlaylistDialog::OnMoveUp() {
    if (m_playlist.empty()) return;
    HWND hListBox = GetDlgItem(m_hWnd, IDC_LISTBOX);
    LRESULT sel = SendMessageW(hListBox, LB_GETCURSEL, 0, 0);
    if (sel != LB_ERR && sel > 0 && sel < (LRESULT)m_playlist.size()) {
        std::swap(m_playlist[sel], m_playlist[sel - 1]);
        
        // Update current playing index if it was affected
        if (m_currentIndex == static_cast<size_t>(sel)) {
            m_currentIndex = sel - 1;
        } else if (m_currentIndex == static_cast<size_t>(sel - 1)) {
            m_currentIndex = sel;
        }
        
        RefreshListBox();
        SendMessageW(hListBox, LB_SETCURSEL, sel - 1, 0);
    }
}

void PlaylistDialog::OnMoveDown() {
    if (m_playlist.empty()) return;
    HWND hListBox = GetDlgItem(m_hWnd, IDC_LISTBOX);
    LRESULT sel = SendMessageW(hListBox, LB_GETCURSEL, 0, 0);
    if (sel != LB_ERR && sel >= 0 && sel < (LRESULT)(m_playlist.size() - 1)) {
        std::swap(m_playlist[sel], m_playlist[sel + 1]);
        
        // Update current playing index if it was affected
        if (m_currentIndex == static_cast<size_t>(sel)) {
            m_currentIndex = sel + 1;
        } else if (m_currentIndex == static_cast<size_t>(sel + 1)) {
            m_currentIndex = sel;
        }
        
        RefreshListBox();
        SendMessageW(hListBox, LB_SETCURSEL, sel + 1, 0);
    }
}

void PlaylistDialog::OnSave() {
    if (m_onPlaylistUpdated) {
        m_onPlaylistUpdated(m_playlist, m_currentIndex);
    }
    Hide();
}

void PlaylistDialog::OnCancel() {
    m_playlist = m_originalPlaylist;
    m_currentIndex = m_originalIndex;
    Hide();
}
