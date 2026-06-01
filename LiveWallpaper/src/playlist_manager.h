#pragma once
#include <string>
#include <vector>
#include <mutex>

class PlaylistManager {
public:
    PlaylistManager();
    ~PlaylistManager();

    void SetPlaylist(const std::vector<std::wstring>& playlist, size_t startIndex = 0);
    std::vector<std::wstring> GetPlaylist() const;

    std::wstring GetCurrentTrack() const;
    size_t GetCurrentIndex() const;

    bool Next();
    bool Previous();
    void Clear();
    bool IsEmpty() const;

    void SetRotationInterval(int minutes);
    int GetRotationInterval() const;

    // Updates the elapsed time and returns true if we need to transition to the next track.
    bool Update(double deltaMs);
    void ResetTimer();

private:
    std::vector<std::wstring> m_playlist;
    size_t m_currentIndex = 0;
    int m_rotationIntervalMinutes = 0;
    double m_elapsedMs = 0.0;
    mutable std::mutex m_mutex;
};
