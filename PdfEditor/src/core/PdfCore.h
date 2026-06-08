#pragma once
#include <fpdfview.h>
#include <string>
#include <memory>
#include <vector>

namespace PdfEditor {

struct PageMetrics {
    int index;
    double width;
    double height;
};

class PdfCore {
public:
    PdfCore();
    ~PdfCore();

    // Prevent copying
    PdfCore(const PdfCore&) = delete;
    PdfCore& operator=(const PdfCore&) = delete;

    bool loadDocument(const std::string& filePath);
    void closeDocument();

    int getPageCount() const;
    PageMetrics getPageMetrics(int pageIndex) const;
    FPDF_DOCUMENT getDocument() const { return m_document; }

private:
    FPDF_DOCUMENT m_document = nullptr;
    std::vector<PageMetrics> m_pageMetrics;
};

} // namespace PdfEditor
