#pragma once
#include <vector>
#include <string>

class Shuffler {
public:
    Shuffler();
    
    void loadPlaylist(const std::vector<std::wstring>& files);
    std::wstring getNextSong();
    std::wstring getPreviousSong();
    
    // Custom shuffle explicitly requested by the user
    void shuffle();

    bool isEmpty() const { return originalPlaylist.empty(); }
    
    // Toggle shuffle
    void setShuffleEnabled(bool enable);
    bool isShuffleEnabled() const { return shuffleEnabled; }
    
    // Playlist helpers
    const std::vector<std::wstring>& getPlaylist() const { return originalPlaylist; }
    int getSongIndexInPlaylist(const std::wstring& songPath) const;
    void selectSong(const std::wstring& filePath);
    
private:
    std::vector<std::wstring> originalPlaylist;
    std::vector<int> shuffledIndices;
    int currentIndex;
    bool shuffleEnabled;
    
    // Generates a true random number based on entropy (CPU ticks, time)
    unsigned int getEntropySeed();
};
