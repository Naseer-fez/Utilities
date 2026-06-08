#pragma once

#include <QAbstractListModel>
#include <memory>
#include <vector>
#include "OverlayItem.h"

namespace PdfEditor {

class OverlayModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        ObjectRole = Qt::UserRole + 1
    };

    explicit OverlayModel(QObject* parent = nullptr);
    ~OverlayModel() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addTextOverlay(int pageIndex, qreal x, qreal y, qreal width = 0.2, qreal height = 0.05);
    Q_INVOKABLE void addHighlightOverlay(int pageIndex, qreal x, qreal y, qreal width = 0.1, qreal height = 0.02);
    Q_INVOKABLE void removeOverlay(int index);

    const std::vector<std::unique_ptr<OverlayItem>>& getOverlays() const;

private:
    std::vector<std::unique_ptr<OverlayItem>> m_overlays;
};

} // namespace PdfEditor
