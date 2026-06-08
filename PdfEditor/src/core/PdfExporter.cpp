#include "PdfExporter.h"
#include <fpdfview.h>
#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fstream>
#include <iostream>
#include <QColor>
#include <QString>

namespace PdfEditor {

struct FileWriter : public FPDF_FILEWRITE {
    std::ofstream stream;

    FileWriter(const std::string& path) {
        version = 1;
        WriteBlock = [](FPDF_FILEWRITE* pThis, const void* pData, unsigned long size) -> int {
            auto* writer = static_cast<FileWriter*>(pThis);
            writer->stream.write(static_cast<const char*>(pData), size);
            return writer->stream.good() ? 1 : 0;
        };
        stream.open(path, std::ios::binary);
    }
};

bool PdfExporter::exportPdf(const std::string& sourcePdf, const std::string& targetPdf, const OverlayModel* model) {
    if (!model) return false;

    // Load the original document
    FPDF_DOCUMENT doc = FPDF_LoadDocument(sourcePdf.c_str(), nullptr);
    if (!doc) {
        return false;
    }

    int pageCount = FPDF_GetPageCount(doc);

    // Prepare standard font for text overlays
    FPDF_FONT font = FPDFText_LoadStandardFont(doc, "Helvetica");

    // We process each page sequentially
    for (int i = 0; i < pageCount; ++i) {
        FPDF_PAGE page = nullptr;
        bool pageModified = false;
        
        // Find overlays for this page
        for (const auto& overlay : model->getOverlays()) {
            if (overlay->pageIndex() == i) {
                if (!page) {
                    page = FPDF_LoadPage(doc, i);
                }
                
                double pageWidth = FPDF_GetPageWidth(page);
                double pageHeight = FPDF_GetPageHeight(page);

                // Convert from QML (normalized, Top-Left) to PDF (points, Bottom-Left)
                qreal qmlX = overlay->x();
                qreal qmlY = overlay->y();
                qreal qmlW = overlay->width();
                qreal qmlH = overlay->height();

                double pdfX = qmlX * pageWidth;
                double pdfW = qmlW * pageWidth;
                double pdfH = qmlH * pageHeight;
                // For PDF, Y is bottom-left origin. 
                // QML Y = 0 is top. So top edge in PDF is pageHeight - (qmlY * pageHeight).
                // Bottom edge in PDF is pageHeight - ((qmlY + qmlH) * pageHeight).
                double pdfY = (1.0 - qmlY - qmlH) * pageHeight;

                if (overlay->type() == OverlayItem::Text) {
                    const TextOverlay* textOverlay = dynamic_cast<const TextOverlay*>(overlay.get());
                    if (textOverlay) {
                        // Create text object. Estimate font size based on height.
                        float fontSize = static_cast<float>(pdfH * 0.8);
                        FPDF_PAGEOBJECT textObj = FPDFPageObj_CreateTextObj(doc, font, fontSize);
                        
                        // Set text color
                        QColor color = overlay->color();
                        FPDFPageObj_SetFillColor(textObj, color.red(), color.green(), color.blue(), color.alpha());
                        
                        // Set text. QString utf16() returns null-terminated unsigned short*
                        QString text = textOverlay->text();
                        FPDFText_SetText(textObj, reinterpret_cast<FPDF_WIDESTRING>(text.utf16()));
                        
                        // Transform to position
                        // The origin of text is typically bottom-left of the first character.
                        FPDFPageObj_Transform(textObj, 1, 0, 0, 1, pdfX, pdfY);
                        
                        // Insert into page
                        FPDFPage_InsertObject(page, textObj);
                        pageModified = true;
                    }
                } else if (overlay->type() == OverlayItem::Highlight) {
                    // Create highlight rect
                    FPDF_PAGEOBJECT rectObj = FPDFPageObj_CreateNewRect(static_cast<float>(pdfX), static_cast<float>(pdfY), static_cast<float>(pdfW), static_cast<float>(pdfH));
                    
                    QColor color = overlay->color();
                    // Set fill color with transparency (e.g. 50% = 128)
                    FPDFPageObj_SetFillColor(rectObj, color.red(), color.green(), color.blue(), 128);
                    
                    // Set blend mode to Multiply for realistic highlight effect
                    FPDFPageObj_SetBlendMode(rectObj, "Multiply");
                    
                    FPDFPage_InsertObject(page, rectObj);
                    pageModified = true;
                }
            }
        }

        if (pageModified && page) {
            FPDFPage_GenerateContent(page);
        }
        
        if (page) {
            FPDF_ClosePage(page);
        }
    }

    FileWriter writer(targetPdf);
    if (!writer.stream.is_open()) {
        FPDF_CloseDocument(doc);
        return false;
    }

    bool success = FPDF_SaveAsCopy(doc, &writer, 0);
    
    FPDF_CloseDocument(doc);
    return success;
}

} // namespace PdfEditor
