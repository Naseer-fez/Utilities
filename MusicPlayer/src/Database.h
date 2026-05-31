#pragma once
#include <string>
#include <unordered_map>

class Database {
public:
    Database();
    
    void load(const std::wstring& dbFile);
    void save();
    
    void setRating(const std::wstring& songPath, int rating);
    int getRating(const std::wstring& songPath) const;

private:
    std::wstring dbFilePath;
    std::unordered_map<std::wstring, int> ratings;
};
