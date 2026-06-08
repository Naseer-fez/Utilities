#pragma once
#include <QAbstractListModel>
#include <QRectF>
#include <QString>
#include <vector>

namespace PdfEditor {

class TextSelectionModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int pageIndex READ pageIndex NOTIFY pageIndexChanged)
    Q_PROPERTY(int startIndex READ startIndex NOTIFY startIndexChanged)
    Q_PROPERTY(int endIndex READ endIndex NOTIFY endIndexChanged)
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY hasSelectionChanged)

public:
    enum SelectionRoles {
        RectRole = Qt::UserRole + 1
    };

    explicit TextSelectionModel(QObject* parent = nullptr);
    ~TextSelectionModel() override = default;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int pageIndex() const { return m_pageIndex; }
    int startIndex() const { return m_startIndex; }
    int endIndex() const { return m_endIndex; }
    QString text() const { return m_text; }
    bool hasSelection() const { return m_pageIndex != -1 && m_startIndex != -1 && m_endIndex != -1; }

    void setSelection(int pageIndex, int startIndex, int endIndex, const QString& text, const std::vector<QRectF>& normalizedRects);
    Q_INVOKABLE void clear();

signals:
    void pageIndexChanged();
    void startIndexChanged();
    void endIndexChanged();
    void textChanged();
    void hasSelectionChanged();

private:
    int m_pageIndex = -1;
    int m_startIndex = -1;
    int m_endIndex = -1;
    QString m_text;
    std::vector<QRectF> m_rects; // Normalized QML coordinates
};

} // namespace PdfEditor
