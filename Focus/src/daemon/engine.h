#pragma once
#include "../common/state.h"
#include "../common/config.h"
#include <string>
#include <vector>
#include <mutex>

class FocusEngine {
public:
    FocusEngine();
    ~FocusEngine();

    bool initialize();
    void shutdown();

    bool startSession(const Profile& profile, int durationMinutes);
    bool resumeSession(const SessionState& savedState);
    void stopSession(bool restoreEnvironment);
    
    // Check if session has expired
    void tick();

    bool isSessionActive() const;
    const SessionState getState() const; // Return a copy instead of a reference to be thread-safe
    int getTimeRemainingSeconds() const;

    // Unlock flow
    std::wstring requestUnlockCode();
    bool verifyUnlockCode(const std::wstring& code);
    
    // Configuration dynamic reload
    void reloadConfig();

private:
    SessionState m_state;
    std::wstring m_stateFilePath;
    SmtpConfig m_smtp;
    bool m_smtpConfigured;
    mutable std::mutex m_mutex;
    ULONGLONG m_sessionStartTicks;
    ULONGLONG m_sessionDurationSeconds;
    
    // Asynchronous SMTP email sender
    void sendUnlockEmails(const std::wstring& code);

    // Backup and environment restoration helpers
    std::wstring getOriginalWallpaper();
    void setWallpaper(const std::wstring& path);
    int getSystemVolume();
    void setSystemVolume(int volumePercent);
};
