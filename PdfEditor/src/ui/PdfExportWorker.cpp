#include "PdfExportWorker.h"
#include "../core/PdfExporter.h"
#include <fpdf_save.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>

namespace PdfEditor {

struct LocalFileWriter : public FPDF_FILEWRITE {
    std::ofstream stream;

    LocalFileWriter(const std::string& path) {
        version = 1;
        WriteBlock = [](FPDF_FILEWRITE* pThis, const void* pData, unsigned long size) -> int {
            auto* writer = static_cast<LocalFileWriter*>(pThis);
            writer->stream.write(static_cast<const char*>(pData), size);
            return writer->stream.good() ? 1 : 0;
        };
        stream.open(path, std::ios::binary);
    }
};

PdfExportWorker::PdfExportWorker(
    const QString& sourcePath,
    const QString& targetPath,
    std::shared_ptr<PdfCore> pdfCore,
    std::unique_ptr<PdfTextEngine>& textEngine,
    OverlayModel* overlayModel)
    : m_sourcePath(sourcePath)
    , m_targetPath(targetPath)
    , m_pdfCore(pdfCore)
    , m_hasInlineEdits(textEngine != nullptr)
    , m_overlayModel(overlayModel)
{
}

void PdfExportWorker::run() {
    bool success = false;
    std::string outPathStr = m_targetPath.toStdString();
    
    // Generate a unique temporary file path using std::filesystem
    std::filesystem::path tempPath = std::filesystem::temp_directory_path() / (std::filesystem::path(outPathStr).filename().string() + ".tmp");

    if (m_hasInlineEdits && m_pdfCore->getDocument()) {
        {
            LocalFileWriter writer(tempPath.string());
            if (writer.stream.is_open()) {
                if (FPDF_SaveAsCopy(m_pdfCore->getDocument(), &writer, 0)) {
                    success = true;
                }
            }
        }
        
        if (success) {
            success = PdfExporter::exportPdf(tempPath.string(), tempPath.string() + "_final.tmp", m_overlayModel);
            if (success) {
                try {
                    // Atomic rename from temp to target
                    std::filesystem::rename(tempPath.string() + "_final.tmp", outPathStr);
                } catch (const std::filesystem::filesystem_error& e) {
                    spdlog::error("Failed to atomically rename file: {}", e.what());
                    success = false;
                }
            }
            std::error_code ec;
            std::filesystem::remove(tempPath.string() + "_final.tmp", ec);
        }
        
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        
    } else {
        success = PdfExporter::exportPdf(m_sourcePath.toStdString(), tempPath.string(), m_overlayModel);
        if (success) {
            try {
                std::filesystem::rename(tempPath, outPathStr);
            } catch (const std::filesystem::filesystem_error& e) {
                spdlog::error("Failed to atomically rename file: {}", e.what());
                success = false;
            }
        }
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
    }

    emit exportFinished(success, m_targetPath);
}

} // namespace PdfEditor
