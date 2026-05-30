/***********************************************************************
 * QuickFinder - GUI Implementation
 * 
 * Revamped, ultra-premium Win32 dark-themed user interface.
 * High-performance double-buffered rendering, glow-active search borders,
 * custom-styled responsive buttons, native dark list headers & scrollbars,
 * and adaptive column scaling for maximum clarity.
 ***********************************************************************/

#include "gui.h"
#include "resource.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <commctrl.h>
#include <cstdio>
#include <cwchar>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdi32.lib")

// DWMWA_USE_IMMERSIVE_DARK_MODE for dark title bar
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// =====================================================================
// Subclass Procedures Implementation
// =====================================================================

// ---------------------------------------------------------------------
// Edit Control Subclass: Manages reactive glowing border & keyboard nav
// ---------------------------------------------------------------------
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    MainWindow* pMain = reinterpret_cast<MainWindow*>(dwRefData);
    switch (msg) {
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        pMain->RedrawSearchBoxBorder();
        break;

    case WM_KEYDOWN:
        if (wParam == VK_DOWN) {
            HWND lv = pMain->GetListView();
            if (lv && ListView_GetItemCount(lv) > 0) {
                SetFocus(lv);
                // Highlight and select the first item if none is selected
                int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
                if (sel < 0) {
                    ListView_SetItemState(lv, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
                return 0;
            }
        }
        else if (wParam == VK_RETURN) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GUI::ID_OPEN_BTN, BN_CLICKED), 0);
            return 0;
        }
        else if (wParam == VK_ESCAPE) {
            SendMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
            return 0;
        }
        break;

    case WM_CHAR:
        if (wParam == VK_RETURN || wParam == VK_ESCAPE) return 0; // Prevent beep
        if (wParam == 1) { // Ctrl+A (ASCII 1)
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------
// ListView Subclass: Keyboard navigation back to search box
// ---------------------------------------------------------------------
LRESULT CALLBACK ListViewSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    MainWindow* pMain = reinterpret_cast<MainWindow*>(dwRefData);
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_UP) {
            int sel = ListView_GetNextItem(hwnd, -1, LVNI_SELECTED);
            if (sel <= 0) { // On the first item or no selection
                // Clear selection and jump focus back to the search bar
                ListView_SetItemState(hwnd, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
                SetFocus(pMain->GetSearchEdit());
                return 0;
            }
        }
        else if (wParam == VK_RETURN) {
            SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GUI::ID_OPEN_BTN, BN_CLICKED), 0);
            return 0;
        }
        else if (wParam == VK_ESCAPE) {
            SendMessageW(GetParent(hwnd), WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------
// Button Control Subclass: Double-buffered custom-painted themed buttons
// ---------------------------------------------------------------------
LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    MainWindow* pMain = reinterpret_cast<MainWindow*>(dwRefData);
    
    // Retrieve mouse-hover and mouse-pressed states using window properties
    bool hovered = GetPropW(hwnd, L"FF_Hovered") != NULL;
    bool pressed = GetPropW(hwnd, L"FF_Pressed") != NULL;
    bool focused = GetFocus() == hwnd;

    switch (msg) {
    case WM_MOUSEMOVE: {
        if (!hovered) {
            TRACKMOUSEEVENT tme = {};
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
            SetPropW(hwnd, L"FF_Hovered", reinterpret_cast<HANDLE>(TRUE));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE: {
        RemovePropW(hwnd, L"FF_Hovered");
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }
    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        SetPropW(hwnd, L"FF_Pressed", reinterpret_cast<HANDLE>(TRUE));
        SetFocus(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }
    case WM_LBUTTONUP: {
        if (GetCapture() == hwnd) {
            ReleaseCapture();
            RemovePropW(hwnd, L"FF_Pressed");
            InvalidateRect(hwnd, NULL, FALSE);
            
            RECT rc;
            GetClientRect(hwnd, &rc);
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (PtInRect(&rc, pt)) {
                SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED), reinterpret_cast<LPARAM>(hwnd));
            }
        }
        break;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, NULL, FALSE);
        break;
        
    case WM_DESTROY:
        RemovePropW(hwnd, L"FF_Hovered");
        RemovePropW(hwnd, L"FF_Pressed");
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        
        RECT rc;
        GetClientRect(hwnd, &rc);
        
        // Double-buffering to eliminate any control flickering
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HBITMAP oldBM = reinterpret_cast<HBITMAP>(SelectObject(memDC, memBM));
        
        COLORREF bg_color;
        COLORREF border_color = GUI::BORDER_COLOR;
        COLORREF text_color = GUI::TEXT_COLOR;
        
        int btnId = GetDlgCtrlID(hwnd);
        bool isPrimary = (btnId == GUI::ID_OPEN_BTN);
        
        if (isPrimary) {
            // Premium accent color theme
            if (pressed) {
                bg_color = RGB(80, 110, 220);
            } else if (hovered) {
                bg_color = GUI::ACCENT_HOVER;
            } else {
                bg_color = GUI::ACCENT_COLOR;
            }
            border_color = bg_color;
            text_color = RGB(255, 255, 255); // High contrast text
        } else {
            // Dark secondary styling
            if (pressed) {
                bg_color = RGB(45, 45, 55);
            } else if (hovered) {
                bg_color = RGB(50, 50, 60);
                border_color = GUI::ACCENT_COLOR;
            } else {
                bg_color = GUI::BG_LIGHTER;
            }
        }
        
        // Paint background rounded box (GDI RoundRect)
        HBRUSH brush = CreateSolidBrush(bg_color);
        HPEN pen = CreatePen(PS_SOLID, 1, border_color);
        
        HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(memDC, brush));
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(memDC, pen));
        
        RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
        
        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
        
        // Internal dotted focus ring
        if (focused) {
            HPEN focusPen = CreatePen(PS_DOT, 1, GUI::TEXT_DIM);
            HPEN oldFP = reinterpret_cast<HPEN>(SelectObject(memDC, focusPen));
            HBRUSH nullBrush = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
            HBRUSH oldNB = reinterpret_cast<HBRUSH>(SelectObject(memDC, nullBrush));
            
            RoundRect(memDC, rc.left + 3, rc.top + 3, rc.right - 3, rc.bottom - 3, 4, 4);
            
            SelectObject(memDC, oldFP);
            SelectObject(memDC, oldNB);
            DeleteObject(focusPen);
        }
        
        // Button text
        wchar_t text[128];
        GetWindowTextW(hwnd, text, 128);
        
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, text_color);
        
        HFONT hFont = reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0));
        if (!hFont) hFont = pMain->GetMainFont();
        HFONT oldFont = reinterpret_cast<HFONT>(SelectObject(memDC, hFont));
        
        DrawTextW(memDC, text, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        
        SelectObject(memDC, oldFont);
        
        // Draw to screen
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, memDC, 0, 0, SRCCOPY);
        
        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// =====================================================================
// MainWindow Implementation
// =====================================================================

MainWindow::MainWindow(SearchEngine* engine, WordFinderEngine* word_engine)
    : engine_(engine), word_engine_(word_engine) {
}

MainWindow::~MainWindow() {
    if (font_main_)      DeleteObject(font_main_);
    if (font_title_)     DeleteObject(font_title_);
    if (font_mono_)      DeleteObject(font_mono_);
    if (bg_brush_)       DeleteObject(bg_brush_);
    if (bg_light_brush_) DeleteObject(bg_light_brush_);
    if (input_brush_)    DeleteObject(input_brush_);
    if (header_brush_)   DeleteObject(header_brush_);
    if (logo_bmp_)       DeleteObject(logo_bmp_);
}

// ---------------------------------------------------------------------
// Create the main window
// ---------------------------------------------------------------------
bool MainWindow::Create(HINSTANCE hInstance) {
    hInstance_ = hInstance;

    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(icex);
    icex.dwICC  = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; // Handled in double-buffered WM_PAINT
    wc.lpszClassName = L"QuickFinderWindow";
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hIconSm       = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));

    if (!RegisterClassExW(&wc)) return false;

    // Center on screen
    int screen_w = GetSystemMetrics(SM_CXSCREEN);
    int screen_h = GetSystemMetrics(SM_CYSCREEN);
    int x = (screen_w - GUI::WINDOW_WIDTH) / 2;
    int y = (screen_h - GUI::WINDOW_HEIGHT) / 2;

    // Enabled maximize/minimize features natively (WS_OVERLAPPEDWINDOW)
    hwnd_ = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"QuickFinderWindow",
        L"⚡ QuickFinder — Ultra-Fast File Search",
        WS_OVERLAPPEDWINDOW,
        x, y, GUI::WINDOW_WIDTH, GUI::WINDOW_HEIGHT,
        NULL, NULL, hInstance, this
    );

    return hwnd_ != NULL;
}

void MainWindow::Show(int nCmdShow) {
    ShowWindow(hwnd_, nCmdShow);
    UpdateWindow(hwnd_);
}

int MainWindow::RunMessageLoop() {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ---------------------------------------------------------------------
// Window Procedure (static dispatcher)
// ---------------------------------------------------------------------
LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------
// Redraw Search Box Border (Triggered by subclass on focus changes)
// ---------------------------------------------------------------------
void MainWindow::RedrawSearchBoxBorder() {
    if (hwnd_) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        // Paint only the container rect to minimize overhead
        RECT border_rc = {20, 78, rc.right - 210, 108};
        InvalidateRect(hwnd_, &border_rc, FALSE);
    }
}

// ---------------------------------------------------------------------
// Message Handler
// ---------------------------------------------------------------------
LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateFontsAndBrushes();
        EnableDarkTitleBar();
        CreateControls();
        SetupListView();
        
        // Start status update timer (every 500ms)
        SetTimer(hwnd_, GUI::ID_STATUS_TIMER, 500, NULL);
        SetFocus(edit_search_);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd_, &ps);

        RECT rc;
        GetClientRect(hwnd_, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        // Double-buffered rendering of the entire window area
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM = reinterpret_cast<HBITMAP>(SelectObject(memDC, memBM));

        // Fill background
        FillRect(memDC, &rc, bg_brush_);

        // Two-tone header utilizing modern dark grey
        RECT title_rc = {0, 0, w, 50};
        FillRect(memDC, &title_rc, header_brush_);

        int text_offset_x = 20;
        if (logo_bmp_) {
            HDC bmpDC = CreateCompatibleDC(memDC);
            HBITMAP oldBmp = reinterpret_cast<HBITMAP>(SelectObject(bmpDC, logo_bmp_));
            BitBlt(memDC, 20, 9, 32, 32, bmpDC, 0, 0, SRCCOPY);
            SelectObject(bmpDC, oldBmp);
            DeleteDC(bmpDC);
            text_offset_x = 60; // Shift title to make space for the logo
        }

        // Draw title text
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, GUI::ACCENT_COLOR);
        SelectObject(memDC, font_title_);
        RECT text_rc = {text_offset_x, 10, w - 20, 45};
        DrawTextW(memDC, L"⚡ QuickFinder", -1, &text_rc, DT_SINGLELINE | DT_VCENTER);

        // Thin separator below title
        HPEN borderPen = CreatePen(PS_SOLID, 1, GUI::BORDER_COLOR);
        HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(memDC, borderPen));
        MoveToEx(memDC, 0, 50, NULL);
        LineTo(memDC, w, 50);

        // Focus-reactive glowing search input frame
        bool searchFocused = (GetFocus() == edit_search_);
        HBRUSH boxBrush = input_brush_;
        HPEN boxPen = CreatePen(PS_SOLID, 1, searchFocused ? GUI::ACCENT_COLOR : GUI::BORDER_COLOR);
        
        HBRUSH oldBoxBrush = reinterpret_cast<HBRUSH>(SelectObject(memDC, boxBrush));
        HPEN oldBoxPen = reinterpret_cast<HPEN>(SelectObject(memDC, boxPen));
        
        RoundRect(memDC, 20, 78, w - 310, 108, 6, 6);
        
        SelectObject(memDC, oldBoxBrush);
        SelectObject(memDC, oldBoxPen);
        DeleteObject(boxPen);

        // Bounding border around ListView (floats inside beautifully)
        HPEN lvBorderPen = CreatePen(PS_SOLID, 1, GUI::BORDER_COLOR);
        HPEN oldLP = reinterpret_cast<HPEN>(SelectObject(memDC, lvBorderPen));
        HBRUSH nullBrush = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        HBRUSH oldLB = reinterpret_cast<HBRUSH>(SelectObject(memDC, nullBrush));

        int lv_y_border = show_advanced_ ? 149 : 119;
        Rectangle(memDC, 9, lv_y_border, w - 9, h - 39);

        SelectObject(memDC, oldLP);
        SelectObject(memDC, oldLB);
        DeleteObject(lvBorderPen);

        // Custom dark status bar (footer)
        RECT footer_rc = {0, h - 30, w, h};
        FillRect(memDC, &footer_rc, bg_light_brush_);

        // Footer separator line
        MoveToEx(memDC, 0, h - 30, NULL);
        LineTo(memDC, w, h - 30);

        // Render status texts
        SetTextColor(memDC, GUI::TEXT_DIM);
        SelectObject(memDC, font_main_);

        // Part 0: Index status (left side)
        RECT status_rc = {15, h - 30, 300, h};
        DrawTextW(memDC, status_index_.c_str(), -1, &status_rc, DT_SINGLELINE | DT_VCENTER);

        // Part 1: Results count (centered)
        RECT results_rc = {w / 2 - 100, h - 30, w / 2 + 100, h};
        DrawTextW(memDC, status_results_.c_str(), -1, &results_rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);

        // Part 2: Search speed performance metrics (right side)
        RECT perf_rc = {w - 300, h - 30, w - 15, h};
        DrawTextW(memDC, status_time_.c_str(), -1, &perf_rc, DT_SINGLELINE | DT_RIGHT | DT_VCENTER);

        // Cleanup
        SelectObject(memDC, oldPen);
        DeleteObject(borderPen);

        // Copy to screen
        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);

        EndPaint(hwnd_, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, GUI::BG_INPUT);
        SetTextColor(hdc, GUI::TEXT_COLOR);
        return reinterpret_cast<LRESULT>(input_brush_);
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkMode(hdc, TRANSPARENT);
        SetBkColor(hdc, GUI::BG_COLOR);
        SetTextColor(hdc, GUI::TEXT_COLOR);
        return reinterpret_cast<LRESULT>(bg_brush_);
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == GUI::ID_SEARCH_EDIT && HIWORD(wParam) == EN_CHANGE) {
            // Debounce search input (150ms)
            KillTimer(hwnd_, GUI::ID_SEARCH_TIMER);
            SetTimer(hwnd_, GUI::ID_SEARCH_TIMER, 150, NULL);
        }
        else if (LOWORD(wParam) == GUI::ID_REFRESH_BTN) {
            OnRefreshIndex();
        }
        else if (LOWORD(wParam) == GUI::ID_OPEN_BTN) {
            OnOpenSelected();
        }
        else if (LOWORD(wParam) == GUI::ID_ADVANCED_BTN) {
            ToggleAdvancedSearch();
        }
        else if (LOWORD(wParam) >= GUI::ID_RADIO_CONTAINS && LOWORD(wParam) <= GUI::ID_CHK_PATH) {
            if (HIWORD(wParam) == BN_CLICKED) {
                OnSearchChanged();
            }
        }
        return 0;

    case WM_TIMER:
        if (wParam == GUI::ID_SEARCH_TIMER) {
            KillTimer(hwnd_, GUI::ID_SEARCH_TIMER);
            OnSearchChanged();
        }
        else if (wParam == GUI::ID_STATUS_TIMER) {
            UpdateStatusBar();
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR* hdr = reinterpret_cast<NMHDR*>(lParam);
        if (hdr->idFrom == GUI::ID_TAB_CONTROL) {
            if (hdr->code == TCN_SELCHANGE) {
                OnTabChanged();
            }
        }
        else if (hdr->idFrom == GUI::ID_LISTVIEW) {
            if (hdr->code == NM_DBLCLK) {
                OnListViewDoubleClick();
            }
            else if (hdr->code == NM_CLICK) {
                NMITEMACTIVATE* nmia = reinterpret_cast<NMITEMACTIVATE*>(lParam);
                int idx = nmia->iItem;
                if (idx >= 0 && idx < static_cast<int>(results_.size())) {
                    // Copy the full path to clipboard on any column click
                    const std::wstring& path = results_[idx].full_path;
                    if (OpenClipboard(hwnd_)) {
                        EmptyClipboard();
                        size_t bytes = (path.size() + 1) * sizeof(wchar_t);
                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
                        if (hMem) {
                            wchar_t* dest = reinterpret_cast<wchar_t*>(GlobalLock(hMem));
                            if (dest) {
                                memcpy(dest, path.c_str(), bytes);
                                GlobalUnlock(hMem);
                                SetClipboardData(CF_UNICODETEXT, hMem);
                            }
                        }
                        CloseClipboard();
                    }
                    // Visual feedback in status bar
                    status_time_ = L" 📋 Path copied!";
                    if (hwnd_) {
                        RECT rc;
                        GetClientRect(hwnd_, &rc);
                        RECT footer_rc = {0, rc.bottom - 30, rc.right, rc.bottom};
                        InvalidateRect(hwnd_, &footer_rc, FALSE);
                    }
                }
            }
            else if (hdr->code == LVN_GETDISPINFO) {
                NMLVDISPINFO* plvdi = reinterpret_cast<NMLVDISPINFO*>(lParam);
                if (plvdi->item.mask & LVIF_TEXT) {
                    int idx = plvdi->item.iItem;
                    if (idx >= 0 && idx < static_cast<int>(results_.size())) {
                        if (plvdi->item.iSubItem == 0) {
                            plvdi->item.pszText = const_cast<LPWSTR>(results_[idx].filename.c_str());
                        } else if (plvdi->item.iSubItem == 1) {
                            plvdi->item.pszText = const_cast<LPWSTR>(results_[idx].full_path.c_str());
                        }
                    }
                }
            }
            else if (hdr->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lParam);
                switch (cd->nmcd.dwDrawStage) {
                case CDDS_PREPAINT:
                    return CDRF_NOTIFYITEMDRAW;
                case CDDS_ITEMPREPAINT:
                    if (cd->nmcd.dwItemSpec % 2 == 0) {
                        cd->clrTextBk = GUI::BG_COLOR;
                    } else {
                        cd->clrTextBk = GUI::BG_LIGHTER;
                    }
                    cd->clrText = GUI::TEXT_COLOR;

                    if (cd->nmcd.uItemState & CDIS_SELECTED) {
                        cd->clrTextBk = GUI::LIST_SEL_BG;
                        cd->clrText   = GUI::ACCENT_HOVER;
                    }
                    return CDRF_NEWFONT;
                }
            }
        }
        return 0;
    }

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        if (edit_search_) {
            // Sleek borderless edit fits perfectly with vertical padding inside custom rounded frame
            MoveWindow(edit_search_, 28, 83, w - 346, 20, TRUE);
        }
        if (btn_advanced_) {
            MoveWindow(btn_advanced_, w - 300, 78, 90, 30, TRUE);
        }
        if (btn_refresh_) {
            MoveWindow(btn_refresh_, w - 200, 78, 85, 30, TRUE);
        }
        if (btn_open_) {
            MoveWindow(btn_open_, w - 105, 78, 85, 30, TRUE);
        }
        if (tab_control_) {
            MoveWindow(tab_control_, 20, 56, 250, 25, TRUE);
        }
        if (listview_) {
            // Adaptive ListView bounds with footer spacing
            int lv_y = show_advanced_ ? 150 : 120;
            MoveWindow(listview_, 10, lv_y, w - 20, h - lv_y - 40, TRUE);

            // Proportional column resizing (35% name, 65% path)
            RECT lv_rc;
            GetClientRect(listview_, &lv_rc);
            int lv_w = lv_rc.right - lv_rc.left;
            if (lv_w > 0) {
                ListView_SetColumnWidth(listview_, 0, static_cast<int>(lv_w * 0.35));
                ListView_SetColumnWidth(listview_, 1, static_cast<int>(lv_w * 0.65));
            }
        }

        InvalidateRect(hwnd_, NULL, FALSE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 600;
        mmi->ptMinTrackSize.y = 400;
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Fully prevented standard erase background to stop layout flicker

    case WM_DESTROY:
        KillTimer(hwnd_, GUI::ID_STATUS_TIMER);
        KillTimer(hwnd_, GUI::ID_SEARCH_TIMER);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

// ---------------------------------------------------------------------
// Create fonts and brushes
// ---------------------------------------------------------------------
void MainWindow::CreateFontsAndBrushes() {
    font_main_ = CreateFontW(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );

    font_title_ = CreateFontW(
        -20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );

    font_mono_ = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono"
    );

    bg_brush_       = CreateSolidBrush(GUI::BG_COLOR);
    bg_light_brush_ = CreateSolidBrush(GUI::BG_LIGHTER);
    input_brush_    = CreateSolidBrush(GUI::BG_INPUT);
    header_brush_   = CreateSolidBrush(GUI::HEADER_BG);
}

// ---------------------------------------------------------------------
// Enable Windows 10/11 Dark Title Bar
// ---------------------------------------------------------------------
void MainWindow::EnableDarkTitleBar() {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}

// ---------------------------------------------------------------------
// Create child controls
// ---------------------------------------------------------------------
void MainWindow::CreateControls() {
    HINSTANCE hInstance_ = GetModuleHandle(NULL);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right;

    // Load custom logo from embedded resources using LoadBitmapW (more reliable for BITMAP resources)
    logo_bmp_ = LoadBitmapW(hInstance_, MAKEINTRESOURCEW(IDB_LOGO));

    // Search edit box - borderless design
    edit_search_ = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        28, 83, w - 246, 20,
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_SEARCH_EDIT),
        hInstance_, NULL
    );
    SendMessageW(edit_search_, WM_SETFONT, reinterpret_cast<WPARAM>(font_main_), TRUE);
    // Custom cue banner text
    SendMessageW(edit_search_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Type filename or full path..."));

    // Advanced Options Button
    btn_advanced_ = CreateWindowExW(
        0, L"BUTTON", L"⚙ Options",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        w - 300, 78, 90, 30, // Initially placed here, repositioned in WM_SIZE
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_ADVANCED_BTN),
        hInstance_, NULL
    );
    SendMessageW(btn_advanced_, WM_SETFONT, reinterpret_cast<WPARAM>(font_main_), TRUE);
    SetWindowSubclass(btn_advanced_, ButtonSubclassProc, GUI::ID_ADVANCED_BTN, reinterpret_cast<DWORD_PTR>(this));

    // Tab Control
    tab_control_ = CreateWindowExW(
        0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE | TCS_BUTTONS | TCS_FLATBUTTONS,
        20, 50, 300, 25,
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_TAB_CONTROL),
        hInstance_, NULL
    );
    SendMessageW(tab_control_, WM_SETFONT, reinterpret_cast<WPARAM>(font_main_), TRUE);
    
    TCITEMW tci = {0};
    tci.mask = TCIF_TEXT;
    tci.pszText = const_cast<LPWSTR>(L"QuickFinder");
    SendMessageW(tab_control_, TCM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&tci));
    tci.pszText = const_cast<LPWSTR>(L"WordFinder");
    SendMessageW(tab_control_, TCM_INSERTITEMW, 1, reinterpret_cast<LPARAM>(&tci));

    // Subclass Edit control
    SetWindowSubclass(edit_search_, EditSubclassProc, GUI::ID_SEARCH_EDIT, reinterpret_cast<DWORD_PTR>(this));

    // Refresh button - Custom owner-drawn subclass
    btn_refresh_ = CreateWindowExW(
        0, L"BUTTON", L"⟳ Re-index",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        w - 200, 78, 85, 30,
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_REFRESH_BTN),
        hInstance_, NULL
    );
    SendMessageW(btn_refresh_, WM_SETFONT, reinterpret_cast<WPARAM>(font_main_), TRUE);
    SetWindowSubclass(btn_refresh_, ButtonSubclassProc, GUI::ID_REFRESH_BTN, reinterpret_cast<DWORD_PTR>(this));

    // Open button - Custom owner-drawn subclass (Primary Style)
    btn_open_ = CreateWindowExW(
        0, L"BUTTON", L"📂 Open",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        w - 105, 78, 85, 30,
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_OPEN_BTN),
        hInstance_, NULL
    );
    SendMessageW(btn_open_, WM_SETFONT, reinterpret_cast<WPARAM>(font_main_), TRUE);
    SetWindowSubclass(btn_open_, ButtonSubclassProc, GUI::ID_OPEN_BTN, reinterpret_cast<DWORD_PTR>(this));

    // Create Option Radio Buttons and Checkboxes
    radio_contains_ = CreateWindowExW(
        0, L"BUTTON", L"Contains",
        WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
        20, 118, 80, 22,
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_RADIO_CONTAINS),
        hInstance_, NULL
    );
    SendMessageW(radio_contains_, WM_SETFONT, reinterpret_cast<WPARAM>(font_main_), TRUE);
    SendMessageW(radio_contains_, BM_SETCHECK, BST_CHECKED, 0); // Check Contains by default

    radio_starts_ = CreateWindowExW(
        0, L"BUTTON", L"Starts with",
        WS_CHILD | BS_AUTORADIOBUTTON,
        105, 118, 95, 22,
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_RADIO_STARTS),
        hInstance_, NULL
    );
    SendMessageW(radio_starts_, WM_SETFONT, reinterpret_cast<WPARAM>(font_main_), TRUE);

    radio_ends_ = CreateWindowExW(
        0, L"BUTTON", L"Ends with",
        WS_CHILD | BS_AUTORADIOBUTTON,
        205, 118, 90, 22,
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_RADIO_ENDS),
        hInstance_, NULL
    );
    SendMessageW(radio_ends_, WM_SETFONT, reinterpret_cast<WPARAM>(font_main_), TRUE);

    radio_exact_ = CreateWindowExW(
        0, L"BUTTON", L"Exact",
        WS_CHILD | BS_AUTORADIOBUTTON,
        300, 118, 65, 22,
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_RADIO_EXACT),
        hInstance_, NULL
    );
    SendMessageW(radio_exact_, WM_SETFONT, reinterpret_cast<WPARAM>(font_main_), TRUE);

    chk_case_ = CreateWindowExW(
        0, L"BUTTON", L"Case match",
        WS_CHILD | BS_AUTOCHECKBOX,
        375, 118, 100, 22,
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_CHK_CASE),
        hInstance_, NULL
    );
    SendMessageW(chk_case_, WM_SETFONT, reinterpret_cast<WPARAM>(font_main_), TRUE);

    chk_path_ = CreateWindowExW(
        0, L"BUTTON", L"Search path",
        WS_CHILD | BS_AUTOCHECKBOX,
        480, 118, 105, 22,
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_CHK_PATH),
        hInstance_, NULL
    );
    SendMessageW(chk_path_, WM_SETFONT, reinterpret_cast<WPARAM>(font_main_), TRUE);

    // Legacy status bar creation removed completely
    status_bar_ = NULL;
}

// ---------------------------------------------------------------------
// Setup ListView with columns
// ---------------------------------------------------------------------
void MainWindow::SetupListView() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right;

    // Removed legacy WS_EX_CLIENTEDGE 3D border
    listview_ = CreateWindowExW(
        0,
        WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL |
        LVS_SHOWSELALWAYS | LVS_OWNERDATA,
        10, 150, w - 20, rc.bottom - 190,
        hwnd_, reinterpret_cast<HMENU>(GUI::ID_LISTVIEW),
        hInstance_, NULL
    );

    // Apply native dark mode theme to scrollbars and column headers
    SetWindowTheme(listview_, L"DarkMode_Explorer", NULL);

    // Subclass ListView for list shortcuts
    SetWindowSubclass(listview_, ListViewSubclassProc, GUI::ID_LISTVIEW, reinterpret_cast<DWORD_PTR>(this));

    // Extended styles
    ListView_SetExtendedListViewStyle(listview_,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);

    SendMessageW(listview_, WM_SETFONT, reinterpret_cast<WPARAM>(font_mono_), TRUE);

    // Set dark colors for list items
    ListView_SetBkColor(listview_, GUI::BG_COLOR);
    ListView_SetTextBkColor(listview_, GUI::BG_COLOR);
    ListView_SetTextColor(listview_, GUI::TEXT_COLOR);

    // Add columns
    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;

    col.fmt     = LVCFMT_LEFT;
    col.cx      = static_cast<int>((w - 20) * 0.35);
    col.pszText = const_cast<LPWSTR>(L"Filename");
    ListView_InsertColumn(listview_, 0, &col);

    col.cx      = static_cast<int>((w - 20) * 0.65);
    col.pszText = const_cast<LPWSTR>(L"Full Path");
    ListView_InsertColumn(listview_, 1, &col);
}

// ---------------------------------------------------------------------
// Search changed - execute search
// ---------------------------------------------------------------------
void MainWindow::OnSearchChanged() {
    wchar_t buf[1024];
    GetWindowTextW(edit_search_, buf, 1024);
    std::wstring query(buf);

    SearchOptions opts = GetSearchOptions();

    // Check if query AND options are unchanged
    if (query == last_query_ && 
        opts.match_type == last_opts_.match_type && 
        opts.case_sensitive == last_opts_.case_sensitive && 
        opts.search_path == last_opts_.search_path) {
        return;
    }
    last_query_ = query;
    last_opts_ = opts;

    // Check if it's a direct path
    if (query.size() > 2 && query[1] == L':' && (query[2] == L'\\' || query[2] == L'/')) {
        DWORD attr = GetFileAttributesW(query.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES) {
            results_.clear();
            SearchResult sr;
            sr.full_path = query;
            size_t pos = query.find_last_of(L"\\/");
            sr.filename = (pos != std::wstring::npos) ? query.substr(pos + 1) : query;
            results_.push_back(std::move(sr));
            UpdateResults();
            return;
        }
    }

    // Normal search
    if (current_tab_ == 0) {
        engine_->Search(query, results_, opts);
    } else {
        word_engine_->Search(query, results_, opts);
    }
    UpdateResults();
}

// ---------------------------------------------------------------------
// Update the list view with current results (virtual mode)
// ---------------------------------------------------------------------
void MainWindow::UpdateResults() {
    ListView_SetItemCountEx(listview_, static_cast<int>(results_.size()),
                            LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    InvalidateRect(listview_, NULL, TRUE);
    UpdateStatusBar();
}

// ---------------------------------------------------------------------
// Double-click on list item -> open in Explorer
// ---------------------------------------------------------------------
void MainWindow::OnListViewDoubleClick() {
    int sel = ListView_GetNextItem(listview_, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < static_cast<int>(results_.size())) {
        engine_->OpenInExplorer(results_[sel].full_path);
    }
}

// ---------------------------------------------------------------------
// Open button click -> open selected file's folder
// ---------------------------------------------------------------------
void MainWindow::OnOpenSelected() {
    wchar_t buf[1024];
    GetWindowTextW(edit_search_, buf, 1024);
    std::wstring query(buf);

    if (query.size() > 2 && query[1] == L':') {
        if (engine_->OpenInExplorer(query)) return;
    }

    int sel = ListView_GetNextItem(listview_, -1, LVNI_SELECTED);
    if (sel >= 0 && sel < static_cast<int>(results_.size())) {
        engine_->OpenInExplorer(results_[sel].full_path);
    }
}

// ---------------------------------------------------------------------
// Refresh index
// ---------------------------------------------------------------------
void MainWindow::OnRefreshIndex() {
    engine_->StopIndexing();
    results_.clear();
    ListView_SetItemCountEx(listview_, 0, LVSICF_NOINVALIDATEALL);
    InvalidateRect(listview_, NULL, TRUE);
    
    status_index_ = L" ⏳ Re-indexing...";
    if (hwnd_) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        RECT footer_rc = {0, rc.bottom - 30, rc.right, rc.bottom};
        InvalidateRect(hwnd_, &footer_rc, FALSE);
    }
    
    engine_->StartIndexing();
}

// ---------------------------------------------------------------------
// Update status bar text and repaint custom footer
// ---------------------------------------------------------------------
void MainWindow::UpdateStatusBar() {
    wchar_t buf[256];

    // Part 0: Index status
    if (engine_->IsIndexing()) {
        swprintf(buf, 256, L" ⏳ Indexing: %u files | %u dirs",
                 engine_->GetIndexedFileCount(),
                 engine_->GetDirectoryCount());
    } else {
        swprintf(buf, 256, L" ✅ Indexed: %u files | %u dirs",
                 engine_->GetIndexedFileCount(),
                 engine_->GetDirectoryCount());
    }
    status_index_ = buf;

    // Part 1: Search results
    swprintf(buf, 256, L" Results: %zu", results_.size());
    status_results_ = buf;

    // Part 2: Search speed performance metrics
    double last_time = (current_tab_ == 0) ? engine_->GetLastSearchTimeMs() : word_engine_->GetLastSearchTimeMs();
    if (last_time > 0) {
        swprintf(buf, 256, L" Search: %.2f ms", last_time);
    } else {
        swprintf(buf, 256, L" Ready");
    }
    status_time_ = buf;

    // Trigger double-buffered paint updates for the footer only
    if (hwnd_) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        RECT footer_rc = {0, rc.bottom - 30, rc.right, rc.bottom};
        InvalidateRect(hwnd_, &footer_rc, FALSE);
    }
}

// ---------------------------------------------------------------------
// Get Search Options from UI State
// ---------------------------------------------------------------------
SearchOptions MainWindow::GetSearchOptions() {
    SearchOptions opts;
    
    if (radio_contains_ && SendMessageW(radio_contains_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        opts.match_type = SearchOptions::MatchType::CONTAINS;
    } else if (radio_starts_ && SendMessageW(radio_starts_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        opts.match_type = SearchOptions::MatchType::STARTS_WITH;
    } else if (radio_ends_ && SendMessageW(radio_ends_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        opts.match_type = SearchOptions::MatchType::ENDS_WITH;
    } else if (radio_exact_ && SendMessageW(radio_exact_, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        opts.match_type = SearchOptions::MatchType::EXACT;
    }

    if (chk_case_) {
        opts.case_sensitive = (SendMessageW(chk_case_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }
    if (chk_path_) {
        opts.search_path = (SendMessageW(chk_path_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }

    return opts;
}

// ---------------------------------------------------------------------
// Toggle Advanced Search Options
// ---------------------------------------------------------------------
void MainWindow::ToggleAdvancedSearch() {
    show_advanced_ = !show_advanced_;
    
    int cmd = show_advanced_ ? SW_SHOW : SW_HIDE;
    if (radio_contains_) ShowWindow(radio_contains_, cmd);
    if (radio_starts_) ShowWindow(radio_starts_, cmd);
    if (radio_ends_) ShowWindow(radio_ends_, cmd);
    if (radio_exact_) ShowWindow(radio_exact_, cmd);
    if (chk_case_) ShowWindow(chk_case_, cmd);
    if (chk_path_) ShowWindow(chk_path_, cmd);
    
    // Trigger WM_SIZE to layout everything again
    RECT rc;
    GetClientRect(hwnd_, &rc);
    SendMessageW(hwnd_, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
}

// ---------------------------------------------------------------------
// Handle Tab Selection Change
// ---------------------------------------------------------------------
void MainWindow::OnTabChanged() {
    int idx = TabCtrl_GetCurSel(tab_control_);
    if (idx != -1) {
        current_tab_ = idx;
        
        // Change placeholder text based on tab
        if (current_tab_ == 0) {
            SendMessageW(edit_search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Type filename or full path..."));
        } else {
            SendMessageW(edit_search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Type a word to search inside text files..."));
        }
        
        // Force search reload using the active tab engine
        results_.clear();
        UpdateResults();
        OnSearchChanged();
    }
}

