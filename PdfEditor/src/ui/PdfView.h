#pragma once
#include <QQuickItem>
#include <QImage>
#include <memory>
#include "../core/PdfCore.h"
#include "../rendering/RenderEngine.h"
#include "../rendering/TileCache.h"
#include "../core/OverlayModel.h"
#include "../core/PdfTextEngine.h"
#include "../core/TextSelectionModel.h"
#include <QUndoStack>

namespace PdfEditor {

class PdfView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(PdfEditor::OverlayModel* overlayModel READ overlayModel CONSTANT)
    Q_PROPERTY(PdfEditor::TextSelectionModel* textSelectionModel READ textSelectionModel CONSTANT)
    Q_PROPERTY(bool editMode READ editMode WRITE setEditMode NOTIFY editModeChanged)

public:
    PdfView(QQuickItem* parent = nullptr);
    ~PdfView() override;

    QString source() const;
    void setSource(const QString& source);

    double zoom() const;
    void setZoom(double zoom);

    bool darkMode() const;
    void setDarkMode(bool darkMode);

    OverlayModel* overlayModel() const;
    TextSelectionModel* textSelectionModel() const;

    bool editMode() const;
    void setEditMode(bool editMode);

    Q_INVOKABLE QPointF screenToPdf(int pageIndex, const QPointF& screenPt) const;
    Q_INVOKABLE QPointF pdfToScreen(int pageIndex, const QPointF& pdfPt) const;
    
    Q_INVOKABLE void exportDocument(const QString& targetPath);

    // Text Interaction
    Q_INVOKABLE void startTextSelection(int pageIndex, const QPointF& screenPt);
    Q_INVOKABLE void updateTextSelection(int pageIndex, const QPointF& screenPt);
    Q_INVOKABLE void selectWordAt(int pageIndex, const QPointF& screenPt);
    Q_INVOKABLE void selectLineAt(int pageIndex, const QPointF& screenPt);
    Q_INVOKABLE void copySelectionToClipboard();
    
    Q_INVOKABLE QRectF getTextObjectBoundsAt(int pageIndex, const QPointF& screenPt) const;
    Q_INVOKABLE QString getTextObjectTextAt(int pageIndex, const QPointF& screenPt) const;
    Q_INVOKABLE bool modifyTextAt(int pageIndex, const QPointF& screenPt, const QString& newText);
    
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

signals:
    void sourceChanged();
    void zoomChanged();
    void darkModeChanged();
    void editModeChanged();
    void exportFinished(bool success, QString targetPath);

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* updatePaintNodeData) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

private:
    void invalidateTiles();
    void requestVisibleTiles();
    void onTileRendered(TileKey key, QImage image);
    
    QPointF screenToPdfPoints(int pageIndex, const QPointF& screenPt) const;
    QRectF pdfPointsToNormalizedRect(int pageIndex, const QRectF& pdfRect) const;
    void updateSelectionState(int pageIndex, int startChar, int endChar);

    QString m_source;
    double m_zoom = 1.0;
    bool m_darkMode = false;
    bool m_editMode = false;

    std::shared_ptr<PdfCore> m_pdfCore;
    std::shared_ptr<TileCache> m_tileCache;
    std::shared_ptr<RenderEngine> m_renderEngine;
    OverlayModel* m_overlayModel;
    TextSelectionModel* m_textSelectionModel;
    std::unique_ptr<PdfTextEngine> m_textEngine;
    
    int m_dragStartChar = -1;
    int m_selectionPageIndex = -1;
    
    struct PendingTexture {
        TileKey key;
        QImage image;
    };
    std::vector<PendingTexture> m_pendingTextures;
    std::mutex m_textureMutex;
    QUndoStack* m_undoStack = nullptr;
};

} // namespace PdfEditor

