/***********************************************************************
 * FileFinder - Main Entry Point
 * 
 * Initializes COM, creates the search engine, starts background
 * indexing, and launches the GUI window.
 ***********************************************************************/

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <objbase.h>
#include "search_engine.h"
#include "gui.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // Initialize COM (needed for shell operations)
    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    // Create the search engine
    SearchEngine engine;

    // Start background indexing immediately
    engine.StartIndexing();

    // Create and show the GUI
    MainWindow window(&engine);
    if (!window.Create(hInstance)) {
        MessageBoxW(NULL, L"Failed to create window.", L"FileFinder Error", MB_ICONERROR);
        engine.StopIndexing();
        CoUninitialize();
        return 1;
    }

    window.Show(nCmdShow);

    // Run message loop
    int result = window.RunMessageLoop();

    // Cleanup
    engine.StopIndexing();
    CoUninitialize();

    return result;
}
