#include "models/RowListModel.hpp"

namespace omachat::client {

RowListModel::RowListModel(QList<QByteArray> roles, QObject* parent)
    : QAbstractListModel(parent)
    , m_roles(std::move(roles))
{
}

int RowListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant RowListModel::data(const QModelIndex& index, int role) const
{
    const int r = index.row();
    const int k = role - Qt::UserRole - 1;
    if (r < 0 || r >= m_rows.size() || k < 0 || k >= m_roles.size())
        return {};
    return m_rows.at(r).value(QString::fromLatin1(m_roles.at(k)));
}

QHash<int, QByteArray> RowListModel::roleNames() const
{
    QHash<int, QByteArray> names;
    for (int i = 0; i < m_roles.size(); ++i)
        names.insert(Qt::UserRole + 1 + i, m_roles.at(i));
    return names;
}

void RowListModel::setRows(QList<QVariantMap> rows)
{
    bool sameShape = rows.size() == m_rows.size();
    for (qsizetype i = 0; sameShape && i < rows.size(); ++i) {
        sameShape = rows.at(i).value(QStringLiteral("key")) == m_rows.at(i).value(QStringLiteral("key"));
    }
    if (sameShape) {
        for (qsizetype i = 0; i < rows.size(); ++i) {
            if (rows.at(i) != m_rows.at(i)) {
                m_rows[i] = rows.at(i);
                const auto idx = index(static_cast<int>(i));
                emit dataChanged(idx, idx);
            }
        }
        return;
    }
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    emit countChanged();
}

QVariantMap RowListModel::get(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row) : QVariantMap{};
}

int RowListModel::indexOf(const QString& key, const QVariant& value) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).value(key) == value)
            return i;
    }
    return -1;
}

} // namespace omachat::client
