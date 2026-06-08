#include "OverlayItem.h"

namespace PdfEditor {

OverlayItem::OverlayItem(Type type, QObject* parent)
    : QObject(parent), m_type(type)
{
}

int OverlayItem::type() const { return m_type; }

int OverlayItem::pageIndex() const { return m_pageIndex; }
void OverlayItem::setPageIndex(int pageIndex) {
    if (m_pageIndex != pageIndex) {
        m_pageIndex = pageIndex;
        emit pageIndexChanged();
    }
}

qreal OverlayItem::x() const { return m_x; }
void OverlayItem::setX(qreal x) {
    if (!qFuzzyCompare(m_x, x)) {
        m_x = x;
        emit xChanged();
    }
}

qreal OverlayItem::y() const { return m_y; }
void OverlayItem::setY(qreal y) {
    if (!qFuzzyCompare(m_y, y)) {
        m_y = y;
        emit yChanged();
    }
}

qreal OverlayItem::width() const { return m_width; }
void OverlayItem::setWidth(qreal width) {
    if (!qFuzzyCompare(m_width, width)) {
        m_width = width;
        emit widthChanged();
    }
}

qreal OverlayItem::height() const { return m_height; }
void OverlayItem::setHeight(qreal height) {
    if (!qFuzzyCompare(m_height, height)) {
        m_height = height;
        emit heightChanged();
    }
}

QColor OverlayItem::color() const { return m_color; }
void OverlayItem::setColor(const QColor& color) {
    if (m_color != color) {
        m_color = color;
        emit colorChanged();
    }
}

// TextOverlay
TextOverlay::TextOverlay(QObject* parent)
    : OverlayItem(Text, parent)
{
}

QString TextOverlay::text() const { return m_text; }
void TextOverlay::setText(const QString& text) {
    if (m_text != text) {
        m_text = text;
        emit textChanged();
    }
}

// HighlightOverlay
HighlightOverlay::HighlightOverlay(QObject* parent)
    : OverlayItem(Highlight, parent)
{
    m_color = QColor(255, 255, 0, 100); // Default semi-transparent yellow
}

} // namespace PdfEditor
