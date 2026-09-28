#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QList>
#include <QVariantMap>

namespace omachat::client {

// A small list model whose rows are QVariantMaps with a fixed set of keys.
// setRows() keeps views stable: when the row identities are unchanged only
// dataChanged() is emitted (no scroll reset, no delegate churn).
class RowListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    RowListModel(QList<QByteArray> roles, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(QList<QVariantMap> rows);
    const QList<QVariantMap>& rows() const { return m_rows; }
    int count() const { return static_cast<int>(m_rows.size()); }

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOf(const QString& key, const QVariant& value) const;

signals:
    void countChanged();

private:
    QList<QByteArray> m_roles;
    QList<QVariantMap> m_rows;
};

} // namespace omachat::client
