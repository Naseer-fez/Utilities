#include "Database.h"
#include <fstream>
#include <sstream>
#include <codecvt>
#include <locale>

Database::Database() {
}

void Database::load(const std::wstring& dbFile) {
    dbFilePath = dbFile;
    ratings.clear();
    
    // Read as binary or simple conversion
    std::ifstream file(std::string(dbFile.begin(), dbFile.end()));
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        size_t comma = line.find_last_of(',');
        if (comma != std::string::npos) {
            std::string pathUtf8 = line.substr(0, comma);
            std::string ratingStr = line.substr(comma + 1);
            
            std::wstring wpath(pathUtf8.begin(), pathUtf8.end());
            try {
                ratings[wpath] = std::stoi(ratingStr);
            } catch (...) {
                // Ignore parsing errors
            }
        }
    }
}

void Database::save() {
    if (dbFilePath.empty()) return;
    
    std::ofstream file(std::string(dbFilePath.begin(), dbFilePath.end()));
    if (!file.is_open()) return;

    for (const auto& pair : ratings) {
        std::string pathUtf8(pair.first.begin(), pair.first.end());
        file << pathUtf8 << "," << pair.second << "\n";
    }
}

void Database::setRating(const std::wstring& songPath, int rating) {
    ratings[songPath] = rating;
    save();
}

int Database::getRating(const std::wstring& songPath) const {
    auto it = ratings.find(songPath);
    if (it != ratings.end()) {
        return it->second;
    }
    return 0; // 0 means unrated
}
