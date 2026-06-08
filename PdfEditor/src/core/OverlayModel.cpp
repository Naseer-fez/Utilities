#include "OverlayModel.h"

namespace PdfEditor {

OverlayModel::OverlayModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

OverlayModel::~OverlayModel() = default;

int OverlayModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_overlays.size());
}

QVariant OverlayModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_overlays.size() || index.row() < 0)
        return QVariant();

    if (role == ObjectRole) {
        return QVariant::fromValue(m_overlays[index.row()].get());
    }

    return QVariant();
}

QHash<int, QByteArray> OverlayModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[ObjectRole] = "overlayObject";
    return roles;
}

void OverlayModel::addTextOverlay(int pageIndex, qreal x, qreal y, qreal width, qreal height) {
    beginInsertRows(QModelIndex(), m_overlays.size(), m_overlays.size());
    auto item = std::make_unique<TextOverlay>();
    item->setPageIndex(pageIndex);
    item->setX(x);
    item->setY(y);
    item->setWidth(width);
    item->setHeight(height);
    item->setText("New Text");
    m_overlays.push_back(std::move(item));
    endInsertRows();
}

void OverlayModel::addHighlightOverlay(int pageIndex, qreal x, qreal y, qreal width, qreal height) {
    beginInsertRows(QModelIndex(), m_overlays.size(), m_overlays.size());
    auto item = std::make_unique<HighlightOverlay>();
    item->setPageIndex(pageIndex);
    item->setX(x);
    item->setY(y);
    item->setWidth(width);
    item->setHeight(height);
    m_overlays.push_back(std::move(item));
    endInsertRows();
}

void OverlayModel::removeOverlay(int index) {
    if (index >= 0 && index < m_overlays.size()) {
        beginRemoveRows(QModelIndex(), index, index);
        m_overlays.erase(m_overlays.begin() + index);
        endRemoveRows();
    }
}

const std::vector<std::unique_ptr<OverlayItem>>& OverlayModel::getOverlays() const {
    return m_overlays;
}

} // namespace PdfEditor
