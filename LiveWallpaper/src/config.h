#pragma once
#include <string>
#include <vector>

class Config {
public:
    Config();
    ~Config();

    bool Load();
    bool Save();

    std::wstring GetVideoPath() const { return m_videoPath; }
    void SetVideoPath(const std::wstring& path);

    bool IsPaused() const { return m_isPaused; }
    void SetPaused(bool paused);

    std::vector<std::wstring> GetPlaylist() const { return m_playlist; }
    void SetPlaylist(const std::vector<std::wstring>& playlist);

    int GetRotationIntervalMinutes() const { return m_rotationIntervalMinutes; }
    void SetRotationIntervalMinutes(int minutes);

    int GetIdleTimeoutMinutes() const { return m_idleTimeoutMinutes; }
    void SetIdleTimeoutMinutes(int minutes);

    int GetFPSLimit() const { return m_fpsLimit; }
    void SetFPSLimit(int fps);

    void RemoveFromPlaylist(size_t index);


private:
    std::wstring GetConfigFilePath() const;

    std::wstring m_videoPath;
    bool m_isPaused = false;
    std::vector<std::wstring> m_playlist;
    int m_rotationIntervalMinutes = 0;
    int m_idleTimeoutMinutes = 5; // Default to 5 minutes idle timeout
    int m_fpsLimit = 0; // Default to 0 (Unlimited/VSync)
};
