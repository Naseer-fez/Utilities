#pragma once

#include <string>
#include "OverlayModel.h"

namespace PdfEditor {

class PdfExporter {
public:
    /**
     * Exports the original PDF along with the overlays to a new PDF file.
     * @param sourcePdf Path to the source PDF file.
     * @param targetPdf Path where the new PDF will be saved.
     * @param model Pointer to the OverlayModel containing the overlays.
     * @return true if successful, false otherwise.
     */
    static bool exportPdf(const std::string& sourcePdf, const std::string& targetPdf, const OverlayModel* model);
};

} // namespace PdfEditor
