#include "Shuffler.h"
#include <windows.h>
#include <intrin.h> // For __rdtsc()
#include <algorithm>

Shuffler::Shuffler() : currentIndex(-1), shuffleEnabled(true) {
}

unsigned int Shuffler::getEntropySeed() {
    // Combine various entropy sources for true randomness
    
    // 1. High resolution performance counter
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    unsigned int entropy1 = static_cast<unsigned int>(li.QuadPart);
    
    // 2. CPU timestamp counter
    unsigned __int64 rdtsc = __rdtsc();
    unsigned int entropy2 = static_cast<unsigned int>(rdtsc ^ (rdtsc >> 32));
    
    // 3. Process time
    FILETIME creationTime, exitTime, kernelTime, userTime;
    GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime);
    unsigned int entropy3 = kernelTime.dwLowDateTime ^ userTime.dwLowDateTime;
    
    // 4. System time
    unsigned int entropy4 = GetTickCount();

    return entropy1 ^ (entropy2 * 16777619) ^ (entropy3 * 2166136261) ^ entropy4;
}

void Shuffler::shuffle() {
    if (originalPlaylist.empty()) return;
    
    shuffledIndices.clear();
    for (size_t i = 0; i < originalPlaylist.size(); ++i) {
        shuffledIndices.push_back(static_cast<int>(i));
    }
    
    if (shuffleEnabled) {
        // Fisher-Yates shuffle using our custom entropy
        unsigned int seed = getEntropySeed();
        for (int i = static_cast<int>(shuffledIndices.size()) - 1; i > 0; --i) {
            // Simple LCG using the entropy seed
            seed = (1103515245 * seed + 12345) % 2147483648;
            int j = seed % (i + 1);
            std::swap(shuffledIndices[i], shuffledIndices[j]);
        }
    }
    
    currentIndex = 0;
}

void Shuffler::loadPlaylist(const std::vector<std::wstring>& files) {
    originalPlaylist = files;
    shuffle();
}

std::wstring Shuffler::getNextSong() {
    if (originalPlaylist.empty()) return L"";
    
    if (currentIndex >= static_cast<int>(shuffledIndices.size()) - 1) {
        // Reshuffle when reaching the end to keep it infinite
        shuffle();
        return originalPlaylist[shuffledIndices[0]];
    }
    
    currentIndex++;
    return originalPlaylist[shuffledIndices[currentIndex]];
}

std::wstring Shuffler::getPreviousSong() {
    if (originalPlaylist.empty()) return L"";
    
    if (currentIndex <= 0) {
        return originalPlaylist[shuffledIndices[0]];
    }
    
    currentIndex--;
    return originalPlaylist[shuffledIndices[currentIndex]];
}

void Shuffler::setShuffleEnabled(bool enable) {
    if (shuffleEnabled == enable) return;
    shuffleEnabled = enable;
    
    std::wstring currentSong;
    if (currentIndex >= 0 && currentIndex < static_cast<int>(shuffledIndices.size())) {
        currentSong = originalPlaylist[shuffledIndices[currentIndex]];
    }
    
    if (shuffleEnabled) {
        shuffle();
        if (!currentSong.empty()) {
            auto origIt = std::find(originalPlaylist.begin(), originalPlaylist.end(), currentSong);
            if (origIt != originalPlaylist.end()) {
                int origIdx = static_cast<int>(std::distance(originalPlaylist.begin(), origIt));
                auto shufIt = std::find(shuffledIndices.begin(), shuffledIndices.end(), origIdx);
                if (shufIt != shuffledIndices.end()) {
                    std::swap(shuffledIndices[0], *shufIt);
                    currentIndex = 0;
                }
            }
        }
    } else {
        shuffledIndices.clear();
        for (size_t i = 0; i < originalPlaylist.size(); ++i) {
            shuffledIndices.push_back(static_cast<int>(i));
        }
        
        if (!currentSong.empty()) {
            auto origIt = std::find(originalPlaylist.begin(), originalPlaylist.end(), currentSong);
            if (origIt != originalPlaylist.end()) {
                currentIndex = static_cast<int>(std::distance(originalPlaylist.begin(), origIt));
            }
        }
    }
}

int Shuffler::getSongIndexInPlaylist(const std::wstring& songPath) const {
    auto it = std::find(originalPlaylist.begin(), originalPlaylist.end(), songPath);
    if (it != originalPlaylist.end()) {
        return static_cast<int>(std::distance(originalPlaylist.begin(), it));
    }
    return -1;
}

void Shuffler::selectSong(const std::wstring& filePath) {
    if (originalPlaylist.empty()) return;
    
    auto it = std::find(originalPlaylist.begin(), originalPlaylist.end(), filePath);
    if (it != originalPlaylist.end()) {
        int origIdx = static_cast<int>(std::distance(originalPlaylist.begin(), it));
        
        auto shufIt = std::find(shuffledIndices.begin(), shuffledIndices.end(), origIdx);
        if (shufIt != shuffledIndices.end()) {
            currentIndex = static_cast<int>(std::distance(shuffledIndices.begin(), shufIt));
        }
    }
}
