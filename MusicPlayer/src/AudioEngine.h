#pragma once
#include <string>

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool loadAndPlay(const std::wstring& filePath);
    bool loadWithoutPlaying(const std::wstring& filePath);
    void pause();
    void resume();
    void stop();
    bool isPlaying() const;
    void updateStatus();

    // Position, Duration, and Volume
    int getDuration();
    int getPosition();
    void setPosition(int ms);
    void setVolume(int vol);
    int getVolume() const { return currentVolume; }

private:
    std::wstring currentAlias;
    bool playing;
    int cachedDuration; // Cache duration per-song to avoid repeated MCI queries
    int currentVolume;   // Current volume (0-100)
    int generateAliasId();
};