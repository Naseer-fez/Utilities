#pragma once
#include <QImage>
#include <unordered_map>
#include <list>
#include <mutex>

namespace PdfEditor {

struct TileKey {
    int pageIndex;
    int tileX;
    int tileY;
    double scale;
    bool operator==(const TileKey& other) const {
        return pageIndex == other.pageIndex && tileX == other.tileX && tileY == other.tileY && scale == other.scale;
    }
};

struct TileKeyHash {
    std::size_t operator()(const TileKey& k) const {
        return std::hash<int>()(k.pageIndex) ^ (std::hash<int>()(k.tileX) << 1) ^ (std::hash<int>()(k.tileY) << 2) ^ std::hash<double>()(k.scale);
    }
};

class TileCache {
public:
    TileCache(size_t maxMemoryBytes = 500 * 1024 * 1024); // 500 MB default
    
    void put(const TileKey& key, const QImage& image);
    QImage get(const TileKey& key);
    bool contains(const TileKey& key);
    
    void clear();

private:
    void evictIfNeeded();

    size_t m_maxMemoryBytes;
    size_t m_currentMemoryBytes = 0;
    
    std::mutex m_mutex;
    
    using CacheList = std::list<std::pair<TileKey, QImage>>;
    CacheList m_list;
    std::unordered_map<TileKey, CacheList::iterator, TileKeyHash> m_map;
};

} // namespace PdfEditor
