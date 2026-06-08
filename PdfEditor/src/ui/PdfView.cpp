#include "PdfView.h"
#include <QSGNode>
#include <QSGTexture>
#include <QSGSimpleTextureNode>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QClipboard>
#include <QUrl>
#include <QThreadPool>
#include <spdlog/spdlog.h>
#include "../core/PdfExporter.h"
#include "../core/ModifyTextCommand.h"
#include "PdfExportWorker.h"

#include <fstream>

namespace PdfEditor {

PdfView::PdfView(QQuickItem* parent)
    : QQuickItem(parent)
{
    {
        std::ofstream logFile("qml_diagnostics.log", std::ios::app);
        logFile << "CHECKPOINT: PdfView constructor started\n";
    }
    setFlag(ItemHasContents, true);
    
    m_pdfCore = std::make_shared<PdfCore>();
    m_tileCache = std::make_shared<TileCache>();
    m_renderEngine = std::make_shared<RenderEngine>(m_pdfCore, m_tileCache);
    m_overlayModel = new OverlayModel(this);
    m_textSelectionModel = new TextSelectionModel(this);
    m_undoStack = new QUndoStack(this);
}

PdfView::~PdfView() {
}

QString PdfView::source() const { return m_source; }
void PdfView::setSource(const QString& source) {
    if (m_source != source) {
        m_source = source;
        if (!m_source.isEmpty()) {
            QString filePath = QUrl(m_source).isLocalFile() ? QUrl(m_source).toLocalFile() : m_source;
            m_pdfCore->loadDocument(filePath.toStdString());
            m_textEngine = std::make_unique<PdfTextEngine>(m_pdfCore->getDocument());
        } else {
            m_textEngine.reset();
            m_pdfCore->closeDocument();
        }

        // Set implicit size based on the first page metrics
        if (m_pdfCore->getDocument() && m_pdfCore->getPageCount() > 0) {
            auto metrics = m_pdfCore->getPageMetrics(0);
            setImplicitSize(metrics.width * m_zoom, metrics.height * m_zoom);
        } else {
            setImplicitSize(0, 0);
        }

        m_textSelectionModel->clear();
        invalidateTiles();
        emit sourceChanged();
    }
}

double PdfView::zoom() const { return m_zoom; }
void PdfView::setZoom(double zoom) {
    if (m_zoom != zoom) {
        m_zoom = zoom;

        // Set implicit size based on the first page metrics and new zoom
        if (m_pdfCore->getDocument() && m_pdfCore->getPageCount() > 0) {
            auto metrics = m_pdfCore->getPageMetrics(0);
            setImplicitSize(metrics.width * m_zoom, metrics.height * m_zoom);
        }

        invalidateTiles();
        emit zoomChanged();
    }
}

bool PdfView::darkMode() const { return m_darkMode; }
void PdfView::setDarkMode(bool darkMode) {
    if (m_darkMode != darkMode) {
        m_darkMode = darkMode;
        invalidateTiles();
        emit darkModeChanged();
    }
}

OverlayModel* PdfView::overlayModel() const { return m_overlayModel; }
TextSelectionModel* PdfView::textSelectionModel() const { return m_textSelectionModel; }

bool PdfView::editMode() const { return m_editMode; }
void PdfView::setEditMode(bool editMode) {
    if (m_editMode != editMode) {
        m_editMode = editMode;
        if (!m_editMode) {
            m_textSelectionModel->clear();
        }
        emit editModeChanged();
    }
}

QPointF PdfView::screenToPdf(int pageIndex, const QPointF& screenPt) const {
    if (!m_pdfCore->getDocument() || pageIndex >= m_pdfCore->getPageCount() || pageIndex < 0) return QPointF(0, 0);
    auto metrics = m_pdfCore->getPageMetrics(pageIndex);
    
    double scaledWidth = metrics.width * m_zoom;
    double scaledHeight = metrics.height * m_zoom;
    
    return QPointF(screenPt.x() / scaledWidth, screenPt.y() / scaledHeight);
}

QPointF PdfView::pdfToScreen(int pageIndex, const QPointF& pdfPt) const {
    if (!m_pdfCore->getDocument() || pageIndex >= m_pdfCore->getPageCount() || pageIndex < 0) return QPointF(0, 0);
    auto metrics = m_pdfCore->getPageMetrics(pageIndex);
    
    double scaledWidth = metrics.width * m_zoom;
    double scaledHeight = metrics.height * m_zoom;
    
    return QPointF(pdfPt.x() * scaledWidth, pdfPt.y() * scaledHeight);
}

void PdfView::exportDocument(const QString& targetPath) {
    if (m_source.isEmpty()) {
        emit exportFinished(false, targetPath);
        return;
    }
    
    QString filePath = QUrl(m_source).isLocalFile() ? QUrl(m_source).toLocalFile() : m_source;
    QString outPath = QUrl(targetPath).isLocalFile() ? QUrl(targetPath).toLocalFile() : targetPath;

    auto* worker = new PdfExportWorker(filePath, outPath, m_pdfCore, m_textEngine, m_overlayModel);
    
    // Connect the worker's signal to PdfView's signal
    connect(worker, &PdfExportWorker::exportFinished, this, [this](bool success, QString path) {
        emit exportFinished(success, path);
    });
    
    QThreadPool::globalInstance()->start(worker);
}

void PdfView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    requestVisibleTiles();
}

void PdfView::invalidateTiles() {
    m_renderEngine->clearQueue();
    m_tileCache->clear();
    
    {
        std::lock_guard<std::mutex> lock(m_textureMutex);
        m_pendingTextures.clear();
    }
    
    requestVisibleTiles();
    update();
}

void PdfView::requestVisibleTiles() {
    if (width() <= 0 || height() <= 0 || !m_pdfCore->getDocument()) return;

    if (m_pdfCore->getPageCount() > 0) {
        auto metrics = m_pdfCore->getPageMetrics(0);
        int pw = static_cast<int>(metrics.width * m_zoom);
        int ph = static_cast<int>(metrics.height * m_zoom);
        
        if (pw > 4000) pw = 4000;
        if (ph > 4000) ph = 4000;

        TileKey key{0, 0, 0, m_zoom};
        
        RenderRequest req;
        req.key = key;
        req.pixelWidth = pw;
        req.pixelHeight = ph;
        req.darkMode = m_darkMode;
        req.onComplete = [this](TileKey k, QImage img) {
            this->onTileRendered(k, std::move(img));
        };
        
        m_renderEngine->requestTile(req);
    }
}

void PdfView::onTileRendered(TileKey key, QImage image) {
    {
        std::lock_guard<std::mutex> lock(m_textureMutex);
        m_pendingTextures.push_back({key, image});
    }
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}

QSGNode* PdfView::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    QSGNode* rootNode = oldNode;
    if (!rootNode) {
        rootNode = new QSGNode();
    }
    
    std::vector<PendingTexture> texturesToUpload;
    {
        std::lock_guard<std::mutex> lock(m_textureMutex);
        std::swap(texturesToUpload, m_pendingTextures);
    }
    
    if (!texturesToUpload.empty()) {
        while (rootNode->firstChild()) {
            QSGNode* child = rootNode->firstChild();
            rootNode->removeChildNode(child);
            delete child;
        }
        
        for (const auto& tex : texturesToUpload) {
            if (tex.image.isNull()) continue;
            
            QSGSimpleTextureNode* texNode = new QSGSimpleTextureNode();
            QSGTexture* qsgTex = window()->createTextureFromImage(tex.image);
            texNode->setTexture(qsgTex);
            texNode->setOwnsTexture(true);
            texNode->setRect(0, 0, tex.image.width(), tex.image.height());
            rootNode->appendChildNode(texNode);
        }
    }
    
    return rootNode;
}

QPointF PdfView::screenToPdfPoints(int pageIndex, const QPointF& screenPt) const {
    if (!m_pdfCore->getDocument() || pageIndex >= m_pdfCore->getPageCount() || pageIndex < 0) return QPointF(0, 0);
    auto metrics = m_pdfCore->getPageMetrics(pageIndex);
    
    double scaledWidth = metrics.width * m_zoom;
    double scaledHeight = metrics.height * m_zoom;
    
    double normX = screenPt.x() / scaledWidth;
    double normY = screenPt.y() / scaledHeight;
    
    double pdfX = normX * metrics.width;
    double pdfY = (1.0 - normY) * metrics.height;
    
    return QPointF(pdfX, pdfY);
}

QRectF PdfView::pdfPointsToNormalizedRect(int pageIndex, const QRectF& pdfRect) const {
    auto metrics = m_pdfCore->getPageMetrics(pageIndex);
    if (metrics.width <= 0 || metrics.height <= 0) return QRectF();
    
    double normX = pdfRect.x() / metrics.width;
    double normY = 1.0 - ((pdfRect.y() + pdfRect.height()) / metrics.height);
    double normW = pdfRect.width() / metrics.width;
    double normH = pdfRect.height() / metrics.height;
    
    return QRectF(normX, normY, normW, normH);
}

void PdfView::updateSelectionState(int pageIndex, int startChar, int endChar) {
    if (!m_textEngine) return;
    
    if (startChar > endChar) std::swap(startChar, endChar);
    
    int count = endChar - startChar + 1;
    QString selText = m_textEngine->getTextInRange(pageIndex, startChar, count);
    
    auto pdfRects = m_textEngine->getSelectionRects(pageIndex, startChar, endChar);
    std::vector<QRectF> normRects;
    normRects.reserve(pdfRects.size());
    for (const auto& r : pdfRects) {
        normRects.push_back(pdfPointsToNormalizedRect(pageIndex, r));
    }
    
    m_textSelectionModel->setSelection(pageIndex, startChar, endChar, selText, normRects);
}

void PdfView::startTextSelection(int pageIndex, const QPointF& screenPt) {
    if (!m_textEngine || pageIndex < 0 || pageIndex >= m_pdfCore->getPageCount()) return;
    
    QPointF pdfPt = screenToPdfPoints(pageIndex, screenPt);
    int charIdx = m_textEngine->getCharIndexAtPos(pageIndex, pdfPt.x(), pdfPt.y());
    if (charIdx != -1) {
        m_dragStartChar = charIdx;
        m_selectionPageIndex = pageIndex;
        updateSelectionState(pageIndex, charIdx, charIdx);
    } else {
        m_dragStartChar = -1;
        m_selectionPageIndex = -1;
        m_textSelectionModel->clear();
    }
}

void PdfView::updateTextSelection(int pageIndex, const QPointF& screenPt) {
    if (!m_textEngine || pageIndex != m_selectionPageIndex || m_dragStartChar == -1) return;
    
    QPointF pdfPt = screenToPdfPoints(pageIndex, screenPt);
    int charIdx = m_textEngine->getCharIndexAtPos(pageIndex, pdfPt.x(), pdfPt.y());
    if (charIdx != -1) {
        updateSelectionState(pageIndex, m_dragStartChar, charIdx);
    }
}

void PdfView::selectWordAt(int pageIndex, const QPointF& screenPt) {
    if (!m_textEngine || pageIndex < 0 || pageIndex >= m_pdfCore->getPageCount()) return;
    
    QPointF pdfPt = screenToPdfPoints(pageIndex, screenPt);
    int charIdx = m_textEngine->getCharIndexAtPos(pageIndex, pdfPt.x(), pdfPt.y());
    if (charIdx != -1) {
        auto range = m_textEngine->getWordRangeForChar(pageIndex, charIdx);
        if (range.first != -1 && range.second != -1) {
            updateSelectionState(pageIndex, range.first, range.second - 1);
        }
    }
}

void PdfView::selectLineAt(int pageIndex, const QPointF& screenPt) {
    if (!m_textEngine || pageIndex < 0 || pageIndex >= m_pdfCore->getPageCount()) return;
    
    QPointF pdfPt = screenToPdfPoints(pageIndex, screenPt);
    int charIdx = m_textEngine->getCharIndexAtPos(pageIndex, pdfPt.x(), pdfPt.y());
    if (charIdx != -1) {
        auto range = m_textEngine->getLineRangeForChar(pageIndex, charIdx);
        if (range.first != -1 && range.second != -1) {
            updateSelectionState(pageIndex, range.first, range.second - 1);
        }
    }
}

QRectF PdfView::getTextObjectBoundsAt(int pageIndex, const QPointF& screenPt) const {
    if (!m_textEngine || pageIndex < 0 || pageIndex >= m_pdfCore->getPageCount()) return QRectF();
    
    QPointF pdfPt = screenToPdfPoints(pageIndex, screenPt);
    FPDF_PAGE page = FPDF_LoadPage(m_pdfCore->getDocument(), pageIndex);
    if (!page) return QRectF();
    
    QRectF normRect;
    FPDF_PAGEOBJECT textObj = m_textEngine->findTextObjectAtPos(page, pdfPt.x(), pdfPt.y());
    if (textObj) {
        float left = 0, bottom = 0, right = 0, top = 0;
        if (FPDFPageObj_GetBounds(textObj, &left, &bottom, &right, &top)) {
            QRectF pdfRect(left, bottom, right - left, top - bottom);
            normRect = pdfPointsToNormalizedRect(pageIndex, pdfRect);
        }
    }
    FPDF_ClosePage(page);
    return normRect;
}

QString PdfView::getTextObjectTextAt(int pageIndex, const QPointF& screenPt) const {
    if (!m_textEngine || pageIndex < 0 || pageIndex >= m_pdfCore->getPageCount()) return QString();
    
    QPointF pdfPt = screenToPdfPoints(pageIndex, screenPt);
    FPDF_PAGE page = FPDF_LoadPage(m_pdfCore->getDocument(), pageIndex);
    if (!page) return QString();
    
    QString text;
    FPDF_PAGEOBJECT textObj = m_textEngine->findTextObjectAtPos(page, pdfPt.x(), pdfPt.y());
    if (textObj) {
        FPDF_TEXTPAGE textPage = m_textEngine->getTextPage(pageIndex);
        if (textPage) {
            unsigned long len = FPDFTextObj_GetText(textObj, textPage, nullptr, 0);
            if (len > 0) {
                std::vector<unsigned short> buffer(len);
                FPDFTextObj_GetText(textObj, textPage, buffer.data(), len);
                text = QString::fromUtf16(buffer.data());
                if (!text.isEmpty() && text.endsWith('\0')) {
                    text.chop(1);
                }
            }
        }
    }
    FPDF_ClosePage(page);
    return text;
}

bool PdfView::modifyTextAt(int pageIndex, const QPointF& screenPt, const QString& newText) {
    if (!m_textEngine || pageIndex < 0 || pageIndex >= m_pdfCore->getPageCount()) return false;
    
    QPointF pdfPt = screenToPdfPoints(pageIndex, screenPt);
    QString oldText = getTextObjectTextAt(pageIndex, screenPt);
    
    if (oldText == newText) return false;
    
    auto* cmd = new ModifyTextCommand(
        m_textEngine.get(),
        m_pdfCore,
        pageIndex,
        pdfPt.x(),
        pdfPt.y(),
        oldText,
        newText,
        [this]() { invalidateTiles(); }
    );
    m_undoStack->push(cmd);
    return true;
}

void PdfView::undo() {
    if (m_undoStack) {
        m_undoStack->undo();
    }
}

void PdfView::redo() {
    if (m_undoStack) {
        m_undoStack->redo();
    }
}

void PdfView::copySelectionToClipboard() {
    if (m_textSelectionModel && m_textSelectionModel->hasSelection()) {
        QGuiApplication::clipboard()->setText(m_textSelectionModel->text());
        spdlog::info("Copied selection to clipboard.");
    }
}

} // namespace PdfEditor
