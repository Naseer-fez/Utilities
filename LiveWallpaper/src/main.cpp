#include <windows.h>
#include "utils.h"
#include "explorer_integration.h"
#include "render_thread_controller.h"
#include "config.h"
#include "tray_icon.h"
#include "power_monitor.h"
#include "playlist_dialog.h"
#include <sddl.h>

// Helper function to retrieve the current user's SID string dynamically
static std::wstring GetCurrentUserSidString() {
    std::wstring sidString;
    HANDLE hToken = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        DWORD dwSize = 0;
        GetTokenInformation(hToken, TokenUser, NULL, 0, &dwSize);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            std::vector<BYTE> buffer(dwSize);
            PTOKEN_USER pTokenUser = reinterpret_cast<PTOKEN_USER>(buffer.data());
            if (GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize)) {
                LPWSTR pSidStr = NULL;
                if (ConvertSidToStringSidW(pTokenUser->User.Sid, &pSidStr)) {
                    sidString = pSidStr;
                    LocalFree(pSidStr);
                }
            }
        }
        CloseHandle(hToken);
    }
    return sidString;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Single instance check using a named session-local mutex to mitigate cross-session squatting
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = FALSE;
    PSECURITY_DESCRIPTOR pSD = NULL;
    
    // Construct SDDL: Restrict access to Local System, Built-in Administrators, and the Current User SID.
    // Also set a Mandatory Integrity Label SACL to Low Integrity (LW) with No-Write-Up (NW) policy
    // to allow low-integrity processes of the same user to interact with/be protected from the mutex.
    std::wstring sddl = L"D:(A;;GA;;;SY)(A;;GA;;;BA)";
    std::wstring userSid = GetCurrentUserSidString();
    if (!userSid.empty()) {
        sddl += L"(A;;GA;;;" + userSid + L")";
    } else {
        sddl += L"(A;;GA;;;OW)"; // Fallback to Owner Rights
    }
    sddl += L"S:(ML;;NW;;;LW)";

    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &pSD, NULL)) {
        sa.lpSecurityDescriptor = pSD;
    } else {
        sa.lpSecurityDescriptor = NULL;
    }
    HANDLE hMutex = CreateMutexW(&sa, TRUE, L"Local\\LiveWallpaperEngineUniqueMutex_FEZN");
    if (pSD) LocalFree(pSD);
    if (hMutex == NULL) {
        return -1;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0; // Exit silently if another instance is already running
    }

    Utils::InitializeLogging();
    LOG_INFO("LiveWallpaper main application starting.");

    HRESULT hrCOM = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCOM)) {
        LOG_ERROR("CoInitializeEx failed in main thread. HRESULT: 0x%08X", hrCOM);
    }

    ExplorerIntegration host;
    if (!host.Initialize(hInstance)) {
        LOG_ERROR("Failed to initialize Explorer Integration.");
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

    RenderThreadController renderThread;
    renderThread.SetFPSLimit(config.GetFPSLimit());
    // Load video on dedicated render thread
    if (!renderThread.Start(host.GetHWND(), videoPath)) {
        LOG_ERROR("Failed to start RenderThreadController.");
        return -1;
    }
    
    // Apply initial pause state
    renderThread.SetPaused(config.IsPaused());

    bool isSystemPowerPaused = false;
    PowerMonitor powerMonitor;
    if (powerMonitor.Initialize(hInstance)) {
        powerMonitor.SetPauseCallback([&](bool pauseForPower) {
            isSystemPowerPaused = pauseForPower;
            bool effectivePause = config.IsPaused() || isSystemPowerPaused;
            renderThread.SetPaused(effectivePause);
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
        renderThread.RequestChangeVideo(nextVideo);
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
            renderThread.RequestChangeVideo(L"");
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
            renderThread.RequestChangeVideo(currentVideo);
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
            if (Utils::ValidateFilePath(newPath)) {
                playlist.clear();
                playlist.push_back(newPath);
                currentPlaylistItem = 0;
                
                config.SetVideoPath(newPath);
                config.SetPlaylist(playlist);
                config.Save();
                
                trayIcon.UpdateHasPlaylist(!playlist.empty());
                renderThread.RequestChangeVideo(newPath);
            } else {
                LOG_ERROR("Rejected invalid or unsafe dropped file path: %ls", newPath.c_str());
            }
        });
        
        trayIcon.SetTogglePauseCallback([&](bool isPaused) {
            config.SetPaused(isPaused);
            config.Save();
            bool effectivePause = config.IsPaused() || isSystemPowerPaused;
            renderThread.SetPaused(effectivePause);
        });
        
        trayIcon.SetExitCallback([&]() {
            PostQuitMessage(0);
        });

        // Phase 7 playlist callbacks
        trayIcon.SetAddVideoCallback([&](const std::wstring& newPath) {
            if (Utils::ValidateFilePath(newPath)) {
                playlist.push_back(newPath);
                config.SetPlaylist(playlist);
                config.Save();
                trayIcon.UpdateHasPlaylist(!playlist.empty());
                
                if (playlist.size() == 1) {
                    currentPlaylistItem = 0;
                    config.SetVideoPath(newPath);
                    config.Save();
                    renderThread.RequestChangeVideo(newPath);
                }
                LOG_INFO("Added video to playlist: %ls", newPath.c_str());
            } else {
                LOG_ERROR("Rejected invalid or unsafe file path: %ls", newPath.c_str());
            }
        });

        trayIcon.SetClearPlaylistCallback([&]() {
            playlist.clear();
            currentPlaylistItem = 0;
            config.SetPlaylist(playlist);
            config.SetVideoPath(L"");
            config.Save();
            trayIcon.UpdateHasPlaylist(false);
            renderThread.RequestChangeVideo(L"");
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
            renderThread.SetFPSLimit(fps);
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
            LOG_INFO("Host window recreated! Requesting RenderThread recovery on new handle.");
            renderThread.RequestRecreate(currentHWnd);
            lastHWnd = currentHWnd;
        }

        if (currentHWnd && IsWindow(currentHWnd)) {
            RECT rect;
            if (GetClientRect(currentHWnd, &rect)) {
                int w = rect.right - rect.left;
                int h = rect.bottom - rect.top;
                renderThread.RequestResize(w, h);
            }
        }
    }

exit_loop:
    LOG_INFO("Exiting main message loop. Stopping RenderThreadController and cleaning up...");
    powerMonitor.Shutdown();
    trayIcon.Shutdown();
    renderThread.Stop();
    host.Shutdown();

    LOG_INFO("LiveWallpaper main application exiting.");

    if (SUCCEEDED(hrCOM)) {
        CoUninitialize();
    }

    Utils::ShutdownLogging();

    CloseHandle(hMutex);
    return (int)msg.wParam;
}


