#include "PdfCore.h"
#include <spdlog/spdlog.h>
#include <atomic>
#include <fstream>

namespace PdfEditor {

namespace {
    // Global initialized flag since FPDF_InitLibrary is process-wide
    std::atomic<int> g_pdfiumInitCount{0};
}

PdfCore::PdfCore() {
    {
        std::ofstream logFile("qml_diagnostics.log", std::ios::app);
        logFile << "CHECKPOINT: PdfCore constructor started\n";
    }
    if (g_pdfiumInitCount == 0) {
        FPDF_LIBRARY_CONFIG config;
        config.version = 2;
        config.m_pUserFontPaths = nullptr;
        config.m_pIsolate = nullptr;
        config.m_v8EmbedderSlot = 0;
        FPDF_InitLibraryWithConfig(&config);
        spdlog::info("PDFium library initialized.");
    }
    g_pdfiumInitCount++;
}

PdfCore::~PdfCore() {
    closeDocument();
    
    g_pdfiumInitCount--;
    if (g_pdfiumInitCount == 0) {
        FPDF_DestroyLibrary();
        spdlog::info("PDFium library destroyed.");
    }
}

bool PdfCore::loadDocument(const std::string& filePath) {
    closeDocument();
    
    m_document = FPDF_LoadDocument(filePath.c_str(), nullptr);
    if (!m_document) {
        unsigned long err = FPDF_GetLastError();
        spdlog::error("Failed to load PDF document: {}, error code: {}", filePath, err);
        return false;
    }

    int pageCount = FPDF_GetPageCount(m_document);
    m_pageMetrics.reserve(pageCount);

    for (int i = 0; i < pageCount; ++i) {
        double width = 0.0, height = 0.0;
        if (FPDF_GetPageSizeByIndex(m_document, i, &width, &height)) {
            m_pageMetrics.push_back({i, width, height});
        } else {
            spdlog::warn("Failed to get metrics for page {}", i);
            m_pageMetrics.push_back({i, 0.0, 0.0});
        }
    }

    spdlog::info("Loaded PDF document: {}, pages: {}", filePath, pageCount);
    return true;
}

void PdfCore::closeDocument() {
    if (m_document) {
        FPDF_CloseDocument(m_document);
        m_document = nullptr;
        m_pageMetrics.clear();
        spdlog::info("Closed PDF document.");
    }
}

int PdfCore::getPageCount() const {
    return static_cast<int>(m_pageMetrics.size());
}

PageMetrics PdfCore::getPageMetrics(int pageIndex) const {
    if (pageIndex >= 0 && pageIndex < m_pageMetrics.size()) {
        return m_pageMetrics[pageIndex];
    }
    return {-1, 0.0, 0.0};
}

} // namespace PdfEditor
