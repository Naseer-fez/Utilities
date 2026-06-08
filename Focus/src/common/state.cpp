#include "state.h"
#include <fstream>

bool saveSessionState(const SessionState& state, const std::wstring& path) {
    std::wstring tempPath = path + L".tmp";
    SetFileAttributesW(tempPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    std::ofstream f(tempPath.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char*>(&state), sizeof(SessionState));
    f.close();
    
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_HIDDEN);
        return true;
    }
    return false;
}

bool loadSessionState(SessionState& state, const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::in | std::ios::binary);
    if (!f.is_open()) return false;
    f.read(reinterpret_cast<char*>(&state), sizeof(SessionState));
    bool success = (f.gcount() == sizeof(SessionState));
    f.close();
    return success;
}

void deleteSessionState(const std::wstring& path) {
    // Reset file attributes to normal so it can be deleted
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(path.c_str());
}
