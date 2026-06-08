#include "ModifyTextCommand.h"
#include <fpdfview.h>
#include <spdlog/spdlog.h>

namespace PdfEditor {

ModifyTextCommand::ModifyTextCommand(
    PdfTextEngine* textEngine,
    std::shared_ptr<PdfCore> pdfCore,
    int pageIndex,
    double pdfX,
    double pdfY,
    const QString& oldText,
    const QString& newText,
    std::function<void()> invalidateCallback,
    QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_textEngine(textEngine)
    , m_pdfCore(pdfCore)
    , m_pageIndex(pageIndex)
    , m_pdfX(pdfX)
    , m_pdfY(pdfY)
    , m_oldText(oldText)
    , m_newText(newText)
    , m_invalidateCallback(invalidateCallback)
{
    setText(QString("Modify text on page %1").arg(pageIndex + 1));
}

bool ModifyTextCommand::applyText(const QString& text) {
    if (!m_textEngine || !m_pdfCore || !m_pdfCore->getDocument()) return false;
    
    FPDF_PAGE page = FPDF_LoadPage(m_pdfCore->getDocument(), m_pageIndex);
    if (!page) return false;
    
    bool success = m_textEngine->modifyTextAtPos(m_pageIndex, page, m_pdfX, m_pdfY, text);
    FPDF_ClosePage(page);
    
    if (success && m_invalidateCallback) {
        m_invalidateCallback();
    }
    return success;
}

void ModifyTextCommand::undo() {
    spdlog::info("Undoing text modification to: {}", m_oldText.toStdString());
    applyText(m_oldText);
}

void ModifyTextCommand::redo() {
    spdlog::info("Redoing text modification to: {}", m_newText.toStdString());
    applyText(m_newText);
}

} // namespace PdfEditor
