#include "TextSelectionModel.h"

namespace PdfEditor {

TextSelectionModel::TextSelectionModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int TextSelectionModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_rects.size());
}

QVariant TextSelectionModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_rects.size() || index.row() < 0)
        return QVariant();

    if (role == RectRole) {
        return m_rects[index.row()];
    }

    return QVariant();
}

QHash<int, QByteArray> TextSelectionModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[RectRole] = "rect";
    return roles;
}

void TextSelectionModel::setSelection(int pageIndex, int startIndex, int endIndex, const QString& text, const std::vector<QRectF>& normalizedRects) {
    bool hasSelChanged = (hasSelection() != (pageIndex != -1 && startIndex != -1 && endIndex != -1));
    bool pageChanged = (m_pageIndex != pageIndex);
    bool startChanged = (m_startIndex != startIndex);
    bool endChanged = (m_endIndex != endIndex);
    bool textChangedVal = (m_text != text);

    beginResetModel();
    m_pageIndex = pageIndex;
    m_startIndex = startIndex;
    m_endIndex = endIndex;
    m_text = text;
    m_rects = normalizedRects;
    endResetModel();

    if (pageChanged) emit pageIndexChanged();
    if (startChanged) emit startIndexChanged();
    if (endChanged) emit endIndexChanged();
    if (textChangedVal) emit textChanged();
    if (hasSelChanged) emit hasSelectionChanged();
}

void TextSelectionModel::clear() {
    setSelection(-1, -1, -1, QString(), {});
}

} // namespace PdfEditor
