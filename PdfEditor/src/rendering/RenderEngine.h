#pragma once
#include "../core/PdfCore.h"
#include "TileCache.h"
#include <QImage>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

namespace PdfEditor {

struct RenderRequest {
    TileKey key;
    int pixelWidth;
    int pixelHeight;
    bool darkMode;
    // Callback when done
    std::function<void(TileKey, QImage)> onComplete;
};

class RenderEngine {
public:
    RenderEngine(std::shared_ptr<PdfCore> pdfCore, std::shared_ptr<TileCache> tileCache, int numThreads = 4);
    ~RenderEngine();

    void requestTile(const RenderRequest& request);
    void clearQueue();

private:
    void workerThread();
    void renderTile(const RenderRequest& req);
    void applyDarkMode(QImage& image);

    std::shared_ptr<PdfCore> m_pdfCore;
    std::shared_ptr<TileCache> m_tileCache;
    
    std::vector<std::thread> m_workers;
    std::queue<RenderRequest> m_queue;
    
    std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_stop;
};

} // namespace PdfEditor
