#pragma once

#include <QObject>
#include <QColor>
#include <QString>

namespace PdfEditor {

class OverlayItem : public QObject {
    Q_OBJECT
    Q_PROPERTY(int type READ type CONSTANT)
    Q_PROPERTY(int pageIndex READ pageIndex WRITE setPageIndex NOTIFY pageIndexChanged)
    Q_PROPERTY(qreal x READ x WRITE setX NOTIFY xChanged)
    Q_PROPERTY(qreal y READ y WRITE setY NOTIFY yChanged)
    Q_PROPERTY(qreal width READ width WRITE setWidth NOTIFY widthChanged)
    Q_PROPERTY(qreal height READ height WRITE setHeight NOTIFY heightChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)

public:
    enum Type {
        Text = 0,
        Highlight
    };
    Q_ENUM(Type)

    explicit OverlayItem(Type type, QObject* parent = nullptr);
    virtual ~OverlayItem() = default;

    int type() const;

    int pageIndex() const;
    void setPageIndex(int pageIndex);

    qreal x() const;
    void setX(qreal x);

    qreal y() const;
    void setY(qreal y);

    qreal width() const;
    void setWidth(qreal width);

    qreal height() const;
    void setHeight(qreal height);

    QColor color() const;
    void setColor(const QColor& color);

signals:
    void pageIndexChanged();
    void xChanged();
    void yChanged();
    void widthChanged();
    void heightChanged();
    void colorChanged();

protected:
    Type m_type;
    int m_pageIndex = 0;
    qreal m_x = 0.0;
    qreal m_y = 0.0;
    qreal m_width = 0.0;
    qreal m_height = 0.0;
    QColor m_color = Qt::black;
};

class TextOverlay : public OverlayItem {
    Q_OBJECT
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)

public:
    explicit TextOverlay(QObject* parent = nullptr);
    
    QString text() const;
    void setText(const QString& text);

signals:
    void textChanged();

private:
    QString m_text;
};

class HighlightOverlay : public OverlayItem {
    Q_OBJECT
public:
    explicit HighlightOverlay(QObject* parent = nullptr);
};

} // namespace PdfEditor
