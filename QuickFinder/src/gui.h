#pragma once
/***********************************************************************
 * QuickFinder - GUI Header
 * 
 * Lightweight Win32 dark-themed window with:
 *   - Search input box
 *   - Results list view
 *   - Status bar with performance metrics
 ***********************************************************************/

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include "search_engine.h"
#include "word_finder_engine.h"

// =====================================================================
// GUI Constants
// =====================================================================
namespace GUI {
    // Window dimensions
    constexpr int WINDOW_WIDTH    = 780;
    constexpr int WINDOW_HEIGHT   = 560;

    // Control IDs
    constexpr int ID_SEARCH_EDIT  = 1001;
    constexpr int ID_LISTVIEW     = 1002;
    constexpr int ID_STATUS_BAR   = 1003;
    constexpr int ID_REFRESH_BTN  = 1004;
    constexpr int ID_OPEN_BTN     = 1005;
    constexpr int ID_RADIO_CONTAINS = 1006;
    constexpr int ID_RADIO_STARTS   = 1007;
    constexpr int ID_RADIO_ENDS     = 1008;
    constexpr int ID_RADIO_EXACT    = 1009;
    constexpr int ID_CHK_CASE       = 1010;
    constexpr int ID_CHK_PATH       = 1011;
    constexpr int ID_TAB_CONTROL    = 1012;
    constexpr int ID_ADVANCED_BTN   = 1013;
    constexpr int ID_SEARCH_TIMER = 2001;
    constexpr int ID_STATUS_TIMER = 2002;

    // Colors (dark theme)
    constexpr COLORREF BG_COLOR         = RGB(24, 24, 28);
    constexpr COLORREF BG_LIGHTER       = RGB(36, 36, 42);
    constexpr COLORREF BG_INPUT         = RGB(44, 44, 52);
    constexpr COLORREF TEXT_COLOR        = RGB(220, 220, 230);
    constexpr COLORREF TEXT_DIM          = RGB(140, 140, 160);
    constexpr COLORREF ACCENT_COLOR     = RGB(100, 140, 255);
    constexpr COLORREF ACCENT_HOVER     = RGB(130, 165, 255);
    constexpr COLORREF BORDER_COLOR     = RGB(60, 60, 70);
    constexpr COLORREF LIST_SEL_BG      = RGB(50, 60, 90);
    constexpr COLORREF HEADER_BG        = RGB(30, 30, 36);
}

// Subclass procedures
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
LRESULT CALLBACK ListViewSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

// =====================================================================
// MainWindow - Application window manager
// =====================================================================
class MainWindow {
public:
    MainWindow(SearchEngine* engine, WordFinderEngine* word_engine);
    ~MainWindow();

    bool Create(HINSTANCE hInstance);
    void Show(int nCmdShow);
    int  RunMessageLoop();

    // Getters for subclasses
    HWND  GetSearchEdit() const { return edit_search_; }
    HWND  GetListView() const   { return listview_; }
    HFONT GetMainFont() const   { return font_main_; }
    void  RedrawSearchBoxBorder();

private:
    // Window handles
    HWND         hwnd_         = NULL;
    HWND         edit_search_  = NULL;
    HWND         listview_     = NULL;
    HWND         status_bar_   = NULL; // Kept as NULL or legacy compatibility
    HWND         btn_refresh_  = NULL;
    HWND         btn_open_     = NULL;
    HWND         radio_contains_ = NULL;
    HWND         radio_starts_   = NULL;
    HWND         radio_ends_     = NULL;
    HWND         radio_exact_    = NULL;
    HWND         chk_case_       = NULL;
    HWND         chk_path_       = NULL;
    HWND         tab_control_    = NULL;
    HWND         btn_advanced_   = NULL;
    HINSTANCE    hInstance_    = NULL;

    // Fonts & brushes
    HFONT        font_main_    = NULL;
    HFONT        font_title_   = NULL;
    HFONT        font_mono_    = NULL;
    HBRUSH       bg_brush_     = NULL;
    HBRUSH       bg_light_brush_ = NULL;
    HBRUSH       input_brush_  = NULL;
    HBRUSH       header_brush_ = NULL;

    HBITMAP      logo_bmp_     = NULL;

    // Custom status strings
    std::wstring status_index_   = L" Indexing...";
    std::wstring status_results_ = L" Results: 0";
    std::wstring status_time_    = L" Ready";

    // Engine reference
    SearchEngine* engine_      = nullptr;
    WordFinderEngine* word_engine_ = nullptr;

    // Current results
    std::vector<SearchResult> results_;
    std::wstring              last_query_;
    SearchOptions             last_opts_;
    bool                      show_advanced_ = false;
    int                       current_tab_   = 0; // 0 = QuickFinder, 1 = WordFinder

    // Setup helpers
    SearchOptions GetSearchOptions();

    // Window procedure
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Setup helpers
    void CreateControls();
    void CreateFontsAndBrushes();
    void SetupListView();
    void EnableDarkTitleBar();

    // Event handlers
    void OnSearchChanged();
    void OnListViewDoubleClick();
    void OnOpenSelected();
    void OnRefreshIndex();
    void UpdateStatusBar();
    void UpdateResults();
    void ToggleAdvancedSearch();
    void OnTabChanged();
};
