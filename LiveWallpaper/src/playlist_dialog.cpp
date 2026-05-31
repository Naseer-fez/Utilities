#include "playlist_dialog.h"
#include "utils.h"
#include <commctrl.h>
#include <shobjidl.h>

#define IDC_LISTBOX 101
#define IDC_ADD 102
#define IDC_REMOVE 103

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

    if (!m_hWnd) {
        m_hWnd = CreateWindowExW(
            WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
            L"LiveWallpaperPlaylistDialogClass",
            L"Manage Playlist",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 500, 400,
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
                }
                return 0;

            case WM_CLOSE:
                pThis->Hide();
                return 0;

            case WM_DESTROY:
                if (pThis->m_hFont) {
                    DeleteObject(pThis->m_hFont);
                    pThis->m_hFont = nullptr;
                }
                return 0;
        }
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

void PlaylistDialog::OnInitDialog() {
    // Create ListBox
    CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        10, 10, 460, 300, m_hWnd, (HMENU)IDC_LISTBOX, m_hInstance, NULL);

    // Create Add Button
    CreateWindowExW(0, L"BUTTON", L"Add Video...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 320, 120, 30, m_hWnd, (HMENU)IDC_ADD, m_hInstance, NULL);

    // Create Remove Button
    CreateWindowExW(0, L"BUTTON", L"Remove Selected",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        140, 320, 150, 30, m_hWnd, (HMENU)IDC_REMOVE, m_hInstance, NULL);

    // Set a modern font
    NONCLIENTMETRICS metrics = { sizeof(NONCLIENTMETRICS) };
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &metrics, 0);
    m_hFont = CreateFontIndirect(&metrics.lfMessageFont);
    
    SendDlgItemMessage(m_hWnd, IDC_LISTBOX, WM_SETFONT, (WPARAM)m_hFont, MAKELPARAM(TRUE, 0));
    SendDlgItemMessage(m_hWnd, IDC_ADD, WM_SETFONT, (WPARAM)m_hFont, MAKELPARAM(TRUE, 0));
    SendDlgItemMessage(m_hWnd, IDC_REMOVE, WM_SETFONT, (WPARAM)m_hFont, MAKELPARAM(TRUE, 0));
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
                    
                    m_playlist.push_back(selectedPath);
                    RefreshListBox();
                    if (m_onPlaylistUpdated) {
                        m_onPlaylistUpdated(m_playlist, m_currentIndex);
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
        if (m_onPlaylistUpdated) {
            m_onPlaylistUpdated(m_playlist, m_currentIndex);
        }
    }
}
