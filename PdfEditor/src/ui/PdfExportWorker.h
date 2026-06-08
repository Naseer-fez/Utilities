#pragma once

#include <QObject>
#include <QRunnable>
#include <QString>
#include <memory>
#include "../core/PdfCore.h"
#include "../core/OverlayModel.h"
#include "../core/PdfTextEngine.h"

namespace PdfEditor {

class PdfExportWorker : public QObject, public QRunnable {
    Q_OBJECT

public:
    PdfExportWorker(
        const QString& sourcePath,
        const QString& targetPath,
        std::shared_ptr<PdfCore> pdfCore,
        std::unique_ptr<PdfTextEngine>& textEngine,
        OverlayModel* overlayModel
    );

    void run() override;

signals:
    void exportFinished(bool success, QString targetPath);

private:
    QString m_sourcePath;
    QString m_targetPath;
    std::shared_ptr<PdfCore> m_pdfCore;
    // We only check if textEngine exists for inline edits, we shouldn't mutate it.
    bool m_hasInlineEdits; 
    OverlayModel* m_overlayModel; // QAbstractListModel should ideally only be read from main thread, but we assume it's read-only during export
};

} // namespace PdfEditor
