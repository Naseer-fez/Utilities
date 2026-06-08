#pragma once
#include <QUndoCommand>
#include <QString>
#include <memory>
#include <functional>
#include "PdfCore.h"
#include "PdfTextEngine.h"

namespace PdfEditor {

class ModifyTextCommand : public QUndoCommand {
public:
    ModifyTextCommand(
        PdfTextEngine* textEngine,
        std::shared_ptr<PdfCore> pdfCore,
        int pageIndex,
        double pdfX,
        double pdfY,
        const QString& oldText,
        const QString& newText,
        std::function<void()> invalidateCallback,
        QUndoCommand* parent = nullptr
    );

    void undo() override;
    void redo() override;

private:
    bool applyText(const QString& text);

    PdfTextEngine* m_textEngine;
    std::shared_ptr<PdfCore> m_pdfCore;
    int m_pageIndex;
    double m_pdfX;
    double m_pdfY;
    QString m_oldText;
    QString m_newText;
    std::function<void()> m_invalidateCallback;
};

} // namespace PdfEditor
