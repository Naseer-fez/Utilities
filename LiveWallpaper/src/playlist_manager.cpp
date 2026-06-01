#include "playlist_manager.h"

PlaylistManager::PlaylistManager() {}
PlaylistManager::~PlaylistManager() {}

void PlaylistManager::SetPlaylist(const std::vector<std::wstring>& playlist, size_t startIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playlist = playlist;
    m_currentIndex = startIndex;
    if (m_playlist.empty()) {
        m_currentIndex = 0;
    } else if (m_currentIndex >= m_playlist.size()) {
        m_currentIndex = 0;
    }
    m_elapsedMs = 0.0;
}

std::vector<std::wstring> PlaylistManager::GetPlaylist() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playlist;
}

std::wstring PlaylistManager::GetCurrentTrack() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_playlist.empty()) return L"";
    return m_playlist[m_currentIndex];
}

size_t PlaylistManager::GetCurrentIndex() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentIndex;
}

bool PlaylistManager::Next() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_playlist.empty()) return false;
    m_currentIndex = (m_currentIndex + 1) % m_playlist.size();
    m_elapsedMs = 0.0;
    return true;
}

bool PlaylistManager::Previous() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_playlist.empty()) return false;
    if (m_currentIndex == 0) {
        m_currentIndex = m_playlist.size() - 1;
    } else {
        m_currentIndex--;
    }
    m_elapsedMs = 0.0;
    return true;
}

void PlaylistManager::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_playlist.clear();
    m_currentIndex = 0;
    m_elapsedMs = 0.0;
}

bool PlaylistManager::IsEmpty() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_playlist.empty();
}

void PlaylistManager::SetRotationInterval(int minutes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_rotationIntervalMinutes = minutes;
    m_elapsedMs = 0.0;
}

int PlaylistManager::GetRotationInterval() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_rotationIntervalMinutes;
}

bool PlaylistManager::Update(double deltaMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_playlist.size() <= 1 || m_rotationIntervalMinutes <= 0) {
        m_elapsedMs = 0.0;
        return false;
    }

    m_elapsedMs += deltaMs;
    double intervalMs = static_cast<double>(m_rotationIntervalMinutes) * 60.0 * 1000.0;
    if (m_elapsedMs >= intervalMs) {
        m_currentIndex = (m_currentIndex + 1) % m_playlist.size();
        m_elapsedMs = 0.0;
        return true;
    }
    return false;
}

void PlaylistManager::ResetTimer() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_elapsedMs = 0.0;
}
