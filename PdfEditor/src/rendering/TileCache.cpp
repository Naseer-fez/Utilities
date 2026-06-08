#include "TileCache.h"
#include <spdlog/spdlog.h>

namespace PdfEditor {

TileCache::TileCache(size_t maxMemoryBytes) : m_maxMemoryBytes(maxMemoryBytes) {
}

void TileCache::put(const TileKey& key, const QImage& image) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_map.find(key);
    if (it != m_map.end()) {
        m_currentMemoryBytes -= it->second->second.sizeInBytes();
        m_list.erase(it->second);
        m_map.erase(it);
    }
    
    m_list.push_front({key, image});
    m_map[key] = m_list.begin();
    m_currentMemoryBytes += image.sizeInBytes();
    
    evictIfNeeded();
}

QImage TileCache::get(const TileKey& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto it = m_map.find(key);
    if (it == m_map.end()) {
        return QImage();
    }
    
    // Move to front (LRU)
    m_list.splice(m_list.begin(), m_list, it->second);
    return it->second->second;
}

bool TileCache::contains(const TileKey& key) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_map.find(key) != m_map.end();
}

void TileCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_map.clear();
    m_list.clear();
    m_currentMemoryBytes = 0;
}

void TileCache::evictIfNeeded() {
    while (m_currentMemoryBytes > m_maxMemoryBytes && !m_list.empty()) {
        auto last = m_list.back();
        m_currentMemoryBytes -= last.second.sizeInBytes();
        m_map.erase(last.first);
        m_list.pop_back();
    }
}

} // namespace PdfEditor
