#include "config.h"
#include "utils.h"
#include <windows.h>
#include <vector>

Config::Config() {}
Config::~Config() {}

std::wstring Config::GetConfigFilePath() const {
    return Utils::GetAppDataPath() + L"\\config.ini";
}

bool Config::Load() {
    std::wstring path = GetConfigFilePath();
    
    wchar_t videoPath[MAX_PATH];
    GetPrivateProfileStringW(L"Settings", L"VideoPath", L"", videoPath, MAX_PATH, path.c_str());
    m_videoPath = videoPath;

    int paused = GetPrivateProfileIntW(L"Settings", L"Paused", 0, path.c_str());
    m_isPaused = (paused != 0);

    // Load Playlist (using large buffer to accommodate multiple long paths)
    std::vector<wchar_t> playlistBuffer(32768);
    GetPrivateProfileStringW(L"Settings", L"Playlist", L"", playlistBuffer.data(), static_cast<DWORD>(playlistBuffer.size()), path.c_str());
    std::wstring playlistStr(playlistBuffer.data());

    m_playlist.clear();
    if (!playlistStr.empty()) {
        size_t start = 0;
        size_t end = playlistStr.find(L'|');
        while (end != std::wstring::npos) {
            std::wstring item = playlistStr.substr(start, end - start);
            if (!item.empty()) {
                m_playlist.push_back(item);
            }
            start = end + 1;
            end = playlistStr.find(L'|', start);
        }
        std::wstring lastItem = playlistStr.substr(start);
        if (!lastItem.empty()) {
            m_playlist.push_back(lastItem);
        }
    }

    m_rotationIntervalMinutes = GetPrivateProfileIntW(L"Settings", L"RotationInterval", 0, path.c_str());
    m_idleTimeoutMinutes = GetPrivateProfileIntW(L"Settings", L"IdleTimeout", 5, path.c_str());
    m_fpsLimit = GetPrivateProfileIntW(L"Settings", L"FPSLimit", 0, path.c_str());

    return true;
}

bool Config::Save() {
    std::wstring path = GetConfigFilePath();
    
    WritePrivateProfileStringW(L"Settings", L"VideoPath", m_videoPath.c_str(), path.c_str());
    WritePrivateProfileStringW(L"Settings", L"Paused", m_isPaused ? L"1" : L"0", path.c_str());

    // Save Playlist
    std::wstring joinedPlaylist;
    for (size_t i = 0; i < m_playlist.size(); ++i) {
        joinedPlaylist += m_playlist[i];
        if (i + 1 < m_playlist.size()) {
            joinedPlaylist += L"|";
        }
    }
    WritePrivateProfileStringW(L"Settings", L"Playlist", joinedPlaylist.c_str(), path.c_str());

    // Save Rotation Interval
    wchar_t intervalStr[32];
    swprintf_s(intervalStr, L"%d", m_rotationIntervalMinutes);
    WritePrivateProfileStringW(L"Settings", L"RotationInterval", intervalStr, path.c_str());

    // Save Idle Timeout
    wchar_t idleStr[32];
    swprintf_s(idleStr, L"%d", m_idleTimeoutMinutes);
    WritePrivateProfileStringW(L"Settings", L"IdleTimeout", idleStr, path.c_str());

    // Save FPS Limit
    wchar_t fpsStr[32];
    swprintf_s(fpsStr, L"%d", m_fpsLimit);
    WritePrivateProfileStringW(L"Settings", L"FPSLimit", fpsStr, path.c_str());

    return true;
}

void Config::SetVideoPath(const std::wstring& path) {
    m_videoPath = path;
}

void Config::SetPaused(bool paused) {
    m_isPaused = paused;
}

void Config::SetPlaylist(const std::vector<std::wstring>& playlist) {
    m_playlist = playlist;
}

void Config::SetRotationIntervalMinutes(int minutes) {
    m_rotationIntervalMinutes = minutes;
}

void Config::SetIdleTimeoutMinutes(int minutes) {
    m_idleTimeoutMinutes = minutes;
}

void Config::SetFPSLimit(int fps) {
    m_fpsLimit = fps;
}

void Config::RemoveFromPlaylist(size_t index) {
    if (index < m_playlist.size()) {
        m_playlist.erase(m_playlist.begin() + index);
    }
}



