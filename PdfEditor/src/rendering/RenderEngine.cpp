#include "RenderEngine.h"
#include <spdlog/spdlog.h>

namespace PdfEditor {

RenderEngine::RenderEngine(std::shared_ptr<PdfCore> pdfCore, std::shared_ptr<TileCache> tileCache, int numThreads)
    : m_pdfCore(pdfCore), m_tileCache(tileCache), m_stop(false) {
    
    for (int i = 0; i < numThreads; ++i) {
        m_workers.emplace_back(&RenderEngine::workerThread, this);
    }
    spdlog::info("RenderEngine started with {} threads.", numThreads);
}

RenderEngine::~RenderEngine() {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stop = true;
    }
    m_condition.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void RenderEngine::requestTile(const RenderRequest& request) {
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push(request);
    }
    m_condition.notify_one();
}

void RenderEngine::clearQueue() {
    std::lock_guard<std::mutex> lock(m_queueMutex);
    std::queue<RenderRequest> empty;
    std::swap(m_queue, empty);
}

void RenderEngine::workerThread() {
    while (true) {
        RenderRequest req;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_condition.wait(lock, [this] { return m_stop || !m_queue.empty(); });
            
            if (m_stop && m_queue.empty()) {
                return;
            }
            
            req = m_queue.front();
            m_queue.pop();
        }
        
        if (m_tileCache->contains(req.key)) {
            if (req.onComplete) {
                req.onComplete(req.key, m_tileCache->get(req.key));
            }
            continue;
        }

        renderTile(req);
    }
}

void RenderEngine::renderTile(const RenderRequest& req) {
    if (!m_pdfCore || !m_pdfCore->getDocument()) return;

    QImage image(req.pixelWidth, req.pixelHeight, QImage::Format_ARGB32);
    image.fill(Qt::white);

    {
        FPDF_PAGE page = FPDF_LoadPage(m_pdfCore->getDocument(), req.key.pageIndex);
        if (page) {
            FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(
                req.pixelWidth, req.pixelHeight,
                FPDFBitmap_BGRA,
                image.bits(), image.bytesPerLine());
            
            if (bitmap) {
                FPDFBitmap_FillRect(bitmap, 0, 0, req.pixelWidth, req.pixelHeight, 0xFFFFFFFF);
                
                int start_x = -req.key.tileX;
                int start_y = -req.key.tileY;
                
                auto metrics = m_pdfCore->getPageMetrics(req.key.pageIndex);
                int scaled_width = static_cast<int>(metrics.width * req.key.scale);
                int scaled_height = static_cast<int>(metrics.height * req.key.scale);
                
                FPDF_RenderPageBitmap(bitmap, page, start_x, start_y, scaled_width, scaled_height, 0, FPDF_ANNOT | FPDF_LCD_TEXT);
                
                FPDFBitmap_Destroy(bitmap);
            }
            FPDF_ClosePage(page);
        }
    }
    
    if (req.darkMode) {
        applyDarkMode(image);
    }
    
    m_tileCache->put(req.key, image);
    
    if (req.onComplete) {
        req.onComplete(req.key, image);
    }
}

void RenderEngine::applyDarkMode(QImage& image) {
    image.invertPixels(QImage::InvertRgb);
}

} // namespace PdfEditor
