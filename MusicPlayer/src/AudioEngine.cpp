#include "AudioEngine.h"
#include <windows.h>
#include <mmsystem.h>
#include <random>
#include <algorithm>

AudioEngine::AudioEngine() : playing(false), cachedDuration(0), currentVolume(80) {
}

AudioEngine::~AudioEngine() {
    stop();
}

int AudioEngine::generateAliasId() {
    static int id = 0;
    return ++id;
}

static std::wstring GetShortPath(const std::wstring& longPath) {
    wchar_t shortPath[MAX_PATH];
    DWORD result = GetShortPathNameW(longPath.c_str(), shortPath, MAX_PATH);
    if (result == 0 || result > MAX_PATH) {
        return longPath;
    }
    return std::wstring(shortPath);
}

bool AudioEngine::loadAndPlay(const std::wstring& filePath) {
    stop();
    
    currentAlias = L"audio_" + std::to_wstring(generateAliasId());
    std::wstring shortPath = GetShortPath(filePath);
    
    std::wstring ext = filePath.substr(filePath.find_last_of(L".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    
    std::wstring typeHint = L"mpegvideo";
    if (ext == L"wav") {
        typeHint = L"waveaudio";
    } else if (ext == L"mid" || ext == L"midi") {
        typeHint = L"sequencer";
    }
    
    // 3-attempt open strategy
    // Attempt 1: Type-specific (e.g. waveaudio, sequencer, or default mpegvideo)
    std::wstring command = L"open \"" + shortPath + L"\" type " + typeHint + L" alias " + currentAlias;
    MCIERROR err = mciSendStringW(command.c_str(), NULL, 0, NULL);
    if (err != 0) {
        // Attempt 2: Fallback to mpegvideo (if typeHint wasn't already mpegvideo)
        if (typeHint != L"mpegvideo") {
            command = L"open \"" + shortPath + L"\" type mpegvideo alias " + currentAlias;
            err = mciSendStringW(command.c_str(), NULL, 0, NULL);
        }
        
        // Attempt 3: Fallback to auto-detect (no type hint)
        if (err != 0) {
            command = L"open \"" + shortPath + L"\" alias " + currentAlias;
            err = mciSendStringW(command.c_str(), NULL, 0, NULL);
        }
    }
    
    if (err != 0) {
        return false;
    }

    // Set time format to milliseconds BEFORE play (avoids seeking / timing issues)
    std::wstring formatCmd = L"set " + currentAlias + L" time format milliseconds";
    mciSendStringW(formatCmd.c_str(), NULL, 0, NULL);

    // Apply the active volume level to the new alias
    setVolume(currentVolume);

    // Cache duration right after load to prevent repeated status queries on every timer tick
    wchar_t buffer[128];
    std::wstring durationCmd = L"status " + currentAlias + L" length";
    if (mciSendStringW(durationCmd.c_str(), buffer, 128, NULL) == 0) {
        try {
            cachedDuration = std::stoi(buffer);
        } catch (...) {
            cachedDuration = 0;
        }
    } else {
        cachedDuration = 0;
    }

    command = L"play " + currentAlias;
    err = mciSendStringW(command.c_str(), NULL, 0, NULL);
    if (err == 0) {
        playing = true;
        return true;
    }
    
    return false;
}

void AudioEngine::pause() {
    if (playing) {
        std::wstring command = L"pause " + currentAlias;
        mciSendStringW(command.c_str(), NULL, 0, NULL);
        playing = false;
    }
}

void AudioEngine::resume() {
    if (!currentAlias.empty() && !playing) {
        std::wstring command = L"play " + currentAlias;
        mciSendStringW(command.c_str(), NULL, 0, NULL);
        playing = true;
    }
}

void AudioEngine::stop() {
    if (!currentAlias.empty()) {
        std::wstring command = L"stop " + currentAlias;
        mciSendStringW(command.c_str(), NULL, 0, NULL);
        command = L"close " + currentAlias;
        mciSendStringW(command.c_str(), NULL, 0, NULL);
        currentAlias.clear();
    }
    playing = false;
    cachedDuration = 0;
}

bool AudioEngine::isPlaying() const {
    return playing;
}

void AudioEngine::updateStatus() {
    if (currentAlias.empty()) {
        playing = false;
        return;
    }
    
    wchar_t buffer[128];
    std::wstring command = L"status " + currentAlias + L" mode";
    mciSendStringW(command.c_str(), buffer, 128, NULL);
    
    std::wstring status(buffer);
    if (status.find(L"playing") != std::wstring::npos) {
        playing = true;
    } else {
        playing = false;
    }
}

int AudioEngine::getDuration() {
    return cachedDuration;
}

int AudioEngine::getPosition() {
    if (currentAlias.empty()) return 0;
    wchar_t buffer[128];
    std::wstring command = L"status " + currentAlias + L" position";
    mciSendStringW(command.c_str(), buffer, 128, NULL);
    try {
        return std::stoi(buffer);
    } catch (...) {
        return 0;
    }
}

void AudioEngine::setPosition(int ms) {
    if (currentAlias.empty()) return;
    std::wstring command = L"seek " + currentAlias + L" to " + std::to_wstring(ms);
    mciSendStringW(command.c_str(), NULL, 0, NULL);
    if (playing) {
        command = L"play " + currentAlias;
        mciSendStringW(command.c_str(), NULL, 0, NULL);
    }
}

bool AudioEngine::loadWithoutPlaying(const std::wstring& filePath) {
    stop();
    
    currentAlias = L"audio_" + std::to_wstring(generateAliasId());
    std::wstring shortPath = GetShortPath(filePath);
    
    std::wstring ext = filePath.substr(filePath.find_last_of(L".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    
    std::wstring typeHint = L"mpegvideo";
    if (ext == L"wav") {
        typeHint = L"waveaudio";
    } else if (ext == L"mid" || ext == L"midi") {
        typeHint = L"sequencer";
    }
    
    std::wstring command = L"open \"" + shortPath + L"\" type " + typeHint + L" alias " + currentAlias;
    MCIERROR err = mciSendStringW(command.c_str(), NULL, 0, NULL);
    if (err != 0) {
        if (typeHint != L"mpegvideo") {
            command = L"open \"" + shortPath + L"\" type mpegvideo alias " + currentAlias;
            err = mciSendStringW(command.c_str(), NULL, 0, NULL);
        }
        if (err != 0) {
            command = L"open \"" + shortPath + L"\" alias " + currentAlias;
            err = mciSendStringW(command.c_str(), NULL, 0, NULL);
        }
    }
    
    if (err != 0) {
        return false;
    }

    std::wstring formatCmd = L"set " + currentAlias + L" time format milliseconds";
    mciSendStringW(formatCmd.c_str(), NULL, 0, NULL);

    // Apply the active volume level to the loaded alias
    setVolume(currentVolume);

    wchar_t buffer[128];
    std::wstring durationCmd = L"status " + currentAlias + L" length";
    if (mciSendStringW(durationCmd.c_str(), buffer, 128, NULL) == 0) {
        try {
            cachedDuration = std::stoi(buffer);
        } catch (...) {
            cachedDuration = 0;
        }
    } else {
        cachedDuration = 0;
    }

    playing = false;
    return true;
}

void AudioEngine::setVolume(int vol) {
    currentVolume = vol;
    if (!currentAlias.empty()) {
        int mciVol = vol * 10; // scale 0-100 to 0-1000
        std::wstring command = L"setaudio " + currentAlias + L" volume to " + std::to_wstring(mciVol);
        mciSendStringW(command.c_str(), NULL, 0, NULL);
    }
}