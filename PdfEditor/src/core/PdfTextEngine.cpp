#include "PdfTextEngine.h"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace PdfEditor {

PdfTextEngine::PdfTextEngine(FPDF_DOCUMENT document)
    : m_document(document)
{
}

PdfTextEngine::~PdfTextEngine() {
    clearCache();
}

void PdfTextEngine::setDocument(FPDF_DOCUMENT document) {
    clearCache();
    m_document = document;
}

FPDF_TEXTPAGE PdfTextEngine::getTextPage(int pageIndex) const {
    if (!m_document) return nullptr;

    auto it = m_textPages.find(pageIndex);
    if (it != m_textPages.end()) {
        return it->second;
    }

    FPDF_PAGE page = FPDF_LoadPage(m_document, pageIndex);
    if (!page) {
        spdlog::warn("Failed to load FPDF_PAGE for text page extraction at page {}", pageIndex);
        return nullptr;
    }

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (!textPage) {
        spdlog::warn("Failed to load FPDF_TEXTPAGE at page {}", pageIndex);
        FPDF_ClosePage(page);
        return nullptr;
    }

    m_loadedPages[pageIndex] = page;
    m_textPages[pageIndex] = textPage;
    return textPage;
}

void PdfTextEngine::unloadTextPage(int pageIndex) {
    auto itText = m_textPages.find(pageIndex);
    if (itText != m_textPages.end()) {
        FPDFText_ClosePage(itText->second);
        m_textPages.erase(itText);
    }

    auto itPage = m_loadedPages.find(pageIndex);
    if (itPage != m_loadedPages.end()) {
        FPDF_ClosePage(itPage->second);
        m_loadedPages.erase(itPage);
    }
}

void PdfTextEngine::clearCache() {
    for (auto& pair : m_textPages) {
        FPDFText_ClosePage(pair.second);
    }
    m_textPages.clear();

    for (auto& pair : m_loadedPages) {
        FPDF_ClosePage(pair.second);
    }
    m_loadedPages.clear();
}

int PdfTextEngine::getCharCount(int pageIndex) const {
    FPDF_TEXTPAGE textPage = getTextPage(pageIndex);
    if (!textPage) return 0;
    return FPDFText_CountChars(textPage);
}

QString PdfTextEngine::getTextInRange(int pageIndex, int startIndex, int count) const {
    FPDF_TEXTPAGE textPage = getTextPage(pageIndex);
    if (!textPage || count <= 0) return QString();

    std::vector<unsigned short> buffer(count + 1, 0);
    int charsWritten = FPDFText_GetText(textPage, startIndex, count, buffer.data());
    if (charsWritten <= 0) return QString();

    return QString::fromUtf16(buffer.data());
}

int PdfTextEngine::getCharIndexAtPos(int pageIndex, double pdfX, double pdfY, double toleranceX, double toleranceY) const {
    FPDF_TEXTPAGE textPage = getTextPage(pageIndex);
    if (!textPage) return -1;

    return FPDFText_GetCharIndexAtPos(textPage, pdfX, pdfY, toleranceX, toleranceY);
}

std::pair<int, int> PdfTextEngine::getLineRangeForChar(int pageIndex, int charIndex) const {
    FPDF_TEXTPAGE textPage = getTextPage(pageIndex);
    if (!textPage) return {-1, -1};

    int totalChars = FPDFText_CountChars(textPage);
    if (charIndex < 0 || charIndex >= totalChars) return {-1, -1};

    int start = charIndex;
    while (start > 0) {
        unsigned int c = FPDFText_GetUnicode(textPage, start - 1);
        if (c == '\n' || c == '\r') {
            break;
        }
        start--;
    }

    int end = charIndex;
    while (end < totalChars) {
        unsigned int c = FPDFText_GetUnicode(textPage, end);
        if (c == '\n' || c == '\r') {
            break;
        }
        end++;
    }

    return {start, end};
}

std::pair<int, int> PdfTextEngine::getWordRangeForChar(int pageIndex, int charIndex) const {
    FPDF_TEXTPAGE textPage = getTextPage(pageIndex);
    if (!textPage) return {-1, -1};

    int totalChars = FPDFText_CountChars(textPage);
    if (charIndex < 0 || charIndex >= totalChars) return {-1, -1};

    auto isWordChar = [](unsigned int c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };

    int start = charIndex;
    while (start > 0) {
        unsigned int c = FPDFText_GetUnicode(textPage, start - 1);
        if (!isWordChar(c)) {
            break;
        }
        start--;
    }

    int end = charIndex;
    while (end < totalChars) {
        unsigned int c = FPDFText_GetUnicode(textPage, end);
        if (!isWordChar(c)) {
            break;
        }
        end++;
    }

    return {start, end};
}

std::vector<QRectF> PdfTextEngine::getSelectionRects(int pageIndex, int startIndex, int endIndex) const {
    std::vector<QRectF> rects;
    FPDF_TEXTPAGE textPage = getTextPage(pageIndex);
    if (!textPage) return rects;

    if (startIndex > endIndex) std::swap(startIndex, endIndex);

    int count = endIndex - startIndex + 1;
    int rectCount = FPDFText_CountRects(textPage, startIndex, count);
    rects.reserve(rectCount);

    for (int i = 0; i < rectCount; ++i) {
        double left = 0, top = 0, right = 0, bottom = 0;
        if (FPDFText_GetRect(textPage, i, &left, &top, &right, &bottom)) {
            // PDF coordinates: Y increases upwards.
            // We store standard rectangles in PDF coordinates.
            rects.push_back(QRectF(left, bottom, right - left, top - bottom));
        }
    }
    return rects;
}

bool PdfTextEngine::modifyTextAtPos(int pageIndex, FPDF_PAGE page, double pdfX, double pdfY, const QString& newText) {
    if (!page) return false;

    FPDF_PAGEOBJECT textObj = findTextObjectAtPos(page, pdfX, pdfY);
    if (!textObj) return false;

    // Set the text of the page object
    FPDFText_SetText(textObj, reinterpret_cast<FPDF_WIDESTRING>(newText.utf16()));

    // Regenerate page contents
    FPDFPage_GenerateContent(page);

    // Unload the text page from cache so it will be reloaded with the new text next time it is queried
    unloadTextPage(pageIndex);

    return true;
}

FPDF_PAGEOBJECT PdfTextEngine::findTextObjectAtPos(FPDF_PAGE page, double pdfX, double pdfY) {
    if (!page) return nullptr;

    int objCount = FPDFPage_CountObjects(page);
    for (int i = 0; i < objCount; ++i) {
        FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page, i);
        if (FPDFPageObj_GetType(obj) == FPDF_PAGEOBJ_TEXT) {
            float left = 0, bottom = 0, right = 0, top = 0;
            if (FPDFPageObj_GetBounds(obj, &left, &bottom, &right, &top)) {
                // Add a small tolerance margin (e.g. 5.0 points)
                float margin = 5.0f;
                if (pdfX >= (left - margin) && pdfX <= (right + margin) &&
                    pdfY >= (bottom - margin) && pdfY <= (top + margin)) {
                    return obj;
                }
            }
        }
    }
    return nullptr;
}

} // namespace PdfEditor
