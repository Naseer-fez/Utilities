#include <windows.h>
#include "utils.h"
#include "wallpaper_host.h"
#include "render_engine.h"
#include "config.h"
#include "tray_icon.h"
#include "power_monitor.h"
#include "playlist_dialog.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    Utils::InitializeLogging();
    LOG_INFO("LiveWallpaper main application starting.");

    HRESULT hrCOM = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCOM)) {
        LOG_ERROR("CoInitializeEx failed in main thread. HRESULT: 0x%08X", hrCOM);
    }

    WallpaperHost host;
    if (!host.Initialize(hInstance)) {
        LOG_ERROR("Failed to initialize Wallpaper Host.");
        return -1;
    }

    Config config;
    config.Load();
    
    std::wstring videoPath = config.GetVideoPath();
    std::vector<std::wstring> playlist = config.GetPlaylist();

    // Migrate old single video config to playlist if playlist is empty
    if (playlist.empty() && !videoPath.empty()) {
        playlist.push_back(videoPath);
        config.SetPlaylist(playlist);
        config.Save();
    }

    // Provide a default fallback if absolutely nothing is saved
    if (playlist.empty()) {
        videoPath = Utils::FindFallbackVideo();
        if (!videoPath.empty()) {
            playlist.push_back(videoPath);
            config.SetVideoPath(videoPath);
            config.SetPlaylist(playlist);
            config.Save();
        } else {
            LOG_INFO("No fallback video found in user's Videos folder. Initializing with empty playlist.");
        }
    }

    size_t currentPlaylistItem = 0;
    for (size_t i = 0; i < playlist.size(); ++i) {
        if (playlist[i] == videoPath) {
            currentPlaylistItem = i;
            break;
        }
    }

    ULONGLONG lastTransitionTime = GetTickCount64();

    RenderEngine renderEngine;
    renderEngine.SetFPSLimit(config.GetFPSLimit());
    // Load video on dedicated render thread
    if (!renderEngine.Start(host.GetHWND(), videoPath)) {
        LOG_ERROR("Failed to start RenderEngine.");
        return -1;
    }
    
    // Apply initial pause state
    renderEngine.SetPaused(config.IsPaused());

    bool isSystemPowerPaused = false;
    PowerMonitor powerMonitor;
    if (powerMonitor.Initialize(hInstance)) {
        powerMonitor.SetPauseCallback([&](bool pauseForPower) {
            isSystemPowerPaused = pauseForPower;
            bool effectivePause = config.IsPaused() || isSystemPowerPaused;
            renderEngine.SetPaused(effectivePause);
            LOG_INFO("Power state changed. Effective pause state: %d", effectivePause);
        });
    } else {
        LOG_WARN("Failed to initialize Power Monitor.");
    }

    auto TransitionToNextVideo = [&]() {
        if (playlist.empty()) return;
        currentPlaylistItem = (currentPlaylistItem + 1) % playlist.size();
        std::wstring nextVideo = playlist[currentPlaylistItem];
        config.SetVideoPath(nextVideo);
        config.Save();
        renderEngine.RequestChangeVideo(nextVideo);
        lastTransitionTime = GetTickCount64();
        LOG_INFO("Transitioned to next video: %ls (index: %zu)", nextVideo.c_str(), currentPlaylistItem);
    };
    TrayIcon trayIcon;
    PlaylistDialog playlistDialog;
    playlistDialog.Initialize(hInstance);
    
    playlistDialog.SetOnPlaylistUpdatedCallback([&](const std::vector<std::wstring>& updatedPlaylist, size_t newCurrentIndex) {
        if (updatedPlaylist.empty()) {
            playlist.clear();
            currentPlaylistItem = 0;
            config.SetPlaylist(playlist);
            config.SetVideoPath(L"");
            config.Save();
            trayIcon.UpdateHasPlaylist(false);
            renderEngine.RequestChangeVideo(L"");
            return;
        }
        
        bool videoChanged = false;
        if (newCurrentIndex < updatedPlaylist.size() && (playlist.empty() || updatedPlaylist[newCurrentIndex] != config.GetVideoPath())) {
            videoChanged = true;
        }
        
        playlist = updatedPlaylist;
        currentPlaylistItem = newCurrentIndex;
        if (currentPlaylistItem >= playlist.size()) currentPlaylistItem = 0;
        
        std::wstring currentVideo = playlist[currentPlaylistItem];
        if (videoChanged || config.GetVideoPath() != currentVideo) {
            config.SetVideoPath(currentVideo);
            renderEngine.RequestChangeVideo(currentVideo);
            lastTransitionTime = GetTickCount64();
        }
        
        config.SetPlaylist(playlist);
        config.Save();
        trayIcon.UpdateHasPlaylist(true);
    });

    if (!trayIcon.Initialize(hInstance)) {
        LOG_WARN("Failed to initialize Tray Icon. Continuing without tray UI.");
    } else {
        trayIcon.UpdatePauseState(config.IsPaused());
        trayIcon.UpdateRotationInterval(config.GetRotationIntervalMinutes());
        trayIcon.UpdateHasPlaylist(!playlist.empty());
        trayIcon.UpdateFPSLimit(config.GetFPSLimit());
        
        trayIcon.SetChangeVideoCallback([&](const std::wstring& newPath) {
            playlist.clear();
            playlist.push_back(newPath);
            currentPlaylistItem = 0;
            config.SetVideoPath(newPath);
            config.SetPlaylist(playlist);
            config.Save();
            
            trayIcon.UpdateHasPlaylist(!playlist.empty());
            renderEngine.RequestChangeVideo(newPath);
        });
        
        trayIcon.SetTogglePauseCallback([&](bool isPaused) {
            config.SetPaused(isPaused);
            config.Save();
            bool effectivePause = config.IsPaused() || isSystemPowerPaused;
            renderEngine.SetPaused(effectivePause);
        });
        
        trayIcon.SetExitCallback([&]() {
            PostQuitMessage(0);
        });

        // Phase 7 playlist callbacks
        trayIcon.SetAddVideoCallback([&](const std::wstring& newPath) {
            playlist.push_back(newPath);
            config.SetPlaylist(playlist);
            config.Save();
            trayIcon.UpdateHasPlaylist(!playlist.empty());
            
            if (playlist.size() == 1) {
                currentPlaylistItem = 0;
                config.SetVideoPath(newPath);
                config.Save();
                renderEngine.RequestChangeVideo(newPath);
            }
            LOG_INFO("Added video to playlist: %ls", newPath.c_str());
        });

        trayIcon.SetClearPlaylistCallback([&]() {
            playlist.clear();
            currentPlaylistItem = 0;
            config.SetPlaylist(playlist);
            config.SetVideoPath(L"");
            config.Save();
            trayIcon.UpdateHasPlaylist(false);
            renderEngine.RequestChangeVideo(L"");
            LOG_INFO("Playlist cleared.");
        });

        trayIcon.SetNextVideoCallback([&]() {
            TransitionToNextVideo();
        });

        trayIcon.SetManagePlaylistCallback([&]() {
            playlistDialog.Show(host.GetHWND(), playlist, currentPlaylistItem);
        });

        trayIcon.SetIntervalCallback([&](int minutes) {
            config.SetRotationIntervalMinutes(minutes);
            config.Save();
            trayIcon.UpdateRotationInterval(minutes);
            lastTransitionTime = GetTickCount64(); // Reset timer on interval change
            LOG_INFO("Rotation interval updated to %d minutes", minutes);
        });

        trayIcon.SetFPSLimitCallback([&](int fps) {
            config.SetFPSLimit(fps);
            config.Save();
            trayIcon.UpdateFPSLimit(fps);
            renderEngine.SetFPSLimit(fps);
            LOG_INFO("FPS limit updated to %d", fps);
        });
    }

    HWND lastHWnd = host.GetHWND();

    MSG msg = { 0 };
    while (true) {
        // Wait for Win32 messages or a 1-second timeout to check watchdog updates (prevents 100% CPU busy-loop)
        MsgWaitForMultipleObjectsEx(0, NULL, 1000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                goto exit_loop;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        // Idle time: check for Explorer watchdog updates
        host.Update();

        // Check power/idle state
        powerMonitor.CheckForegroundAndIdleStates(config.GetIdleTimeoutMinutes());

        // Check rotation interval timer
        int intervalMinutes = config.GetRotationIntervalMinutes();
        if (intervalMinutes > 0 && !playlist.empty() && !(config.IsPaused() || isSystemPowerPaused)) {
            ULONGLONG currentTime = GetTickCount64();
            ULONGLONG intervalMs = static_cast<ULONGLONG>(intervalMinutes) * 60 * 1000;
            if (currentTime - lastTransitionTime >= intervalMs) {
                TransitionToNextVideo();
            }
        } else if (config.IsPaused() || isSystemPowerPaused) {
            // Keep resetting timer while paused so we get a full interval when resumed
            lastTransitionTime = GetTickCount64();
        }

        HWND currentHWnd = host.GetHWND();
        if (currentHWnd != lastHWnd) {
            LOG_INFO("Host window recreated! Requesting RenderEngine recovery on new handle.");
            renderEngine.RequestRecreate(currentHWnd);
            lastHWnd = currentHWnd;
        }

        if (currentHWnd && IsWindow(currentHWnd)) {
            RECT rect;
            if (GetClientRect(currentHWnd, &rect)) {
                int w = rect.right - rect.left;
                int h = rect.bottom - rect.top;
                renderEngine.RequestResize(w, h);
            }
        }
    }

exit_loop:
    LOG_INFO("Exiting main message loop. Stopping RenderEngine and cleaning up...");
    powerMonitor.Shutdown();
    trayIcon.Shutdown();
    renderEngine.Stop();
    host.Shutdown();

    LOG_INFO("LiveWallpaper main application exiting.");

    if (SUCCEEDED(hrCOM)) {
        CoUninitialize();
    }

    return (int)msg.wParam;
}


