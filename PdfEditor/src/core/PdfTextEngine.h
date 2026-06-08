#pragma once
#include <fpdfview.h>
#include <fpdf_text.h>
#include <fpdf_edit.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <QRectF>
#include <QPointF>
#include <QString>

namespace PdfEditor {

class PdfTextEngine {
public:
    explicit PdfTextEngine(FPDF_DOCUMENT document);
    ~PdfTextEngine();

    // Prevent copying
    PdfTextEngine(const PdfTextEngine&) = delete;
    PdfTextEngine& operator=(const PdfTextEngine&) = delete;

    void setDocument(FPDF_DOCUMENT document);

    // Lazy load/unload text page
    FPDF_TEXTPAGE getTextPage(int pageIndex) const;
    void unloadTextPage(int pageIndex);
    void clearCache();

    // Text queries
    int getCharCount(int pageIndex) const;
    QString getTextInRange(int pageIndex, int startIndex, int count) const;
    
    // Position queries
    int getCharIndexAtPos(int pageIndex, double pdfX, double pdfY, double toleranceX = 5.0, double toleranceY = 5.0) const;
    
    // Line/Word ranges
    std::pair<int, int> getLineRangeForChar(int pageIndex, int charIndex) const;
    std::pair<int, int> getWordRangeForChar(int pageIndex, int charIndex) const;

    // Bounding boxes for rendering selection highlights
    std::vector<QRectF> getSelectionRects(int pageIndex, int startIndex, int endIndex) const;

    // Text modification
    // Replaces the text of the text page object at the given PDF coordinate
    bool modifyTextAtPos(int pageIndex, FPDF_PAGE page, double pdfX, double pdfY, const QString& newText);
    
    // Helper to find text object at position
    FPDF_PAGEOBJECT findTextObjectAtPos(FPDF_PAGE page, double pdfX, double pdfY);

private:
    FPDF_DOCUMENT m_document;
    mutable std::unordered_map<int, FPDF_TEXTPAGE> m_textPages;
    mutable std::unordered_map<int, FPDF_PAGE> m_loadedPages;
};

} // namespace PdfEditor
