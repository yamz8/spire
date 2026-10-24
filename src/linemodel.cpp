#include "linemodel.h"

QVariant LineModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const BillingRow &row = m_rows[index.row()];
    switch (role) {
    case IdRole: return row.line.id;
    case NumberRole: return index.row() + 1;
    case CodeRole: return row.line.code;
    case DescriptionRole: return row.line.description;
    case ChangeOrderRole: return row.line.isChangeOrder();
    case CoNumberRole: return row.line.coNumber;
    case ScheduledRole: return double(row.scheduled);
    case PreviousRole: return double(row.previous);
    case ThisPeriodRole: return double(row.thisPeriod);
    case StoredRole: return double(row.stored);
    case CompletedRole: return double(row.completed);
    case ToDateRole: return double(row.toDate);
    case BalanceRole: return double(row.balance);
    case RetainageRole: return double(row.retainage);
    case PercentRole: return row.percentComplete;
    case PercentToDateRole: return row.percentToDate;
    case QuantityRole: return row.line.quantity;
    case UnitRole: return row.line.unit;
    case UnitPriceRole: return double(row.line.unitPrice);
    case InstalledRole: return row.line.quantity * row.percentComplete / 100.0;
    case LockedRole: return m_locked.contains(row.line.id);
    }
    return {};
}

QHash<int, QByteArray> LineModel::roleNames() const {
    return {{IdRole, "lineId"},
            {NumberRole, "number"},
            {CodeRole, "code"},
            {DescriptionRole, "description"},
            {ChangeOrderRole, "changeOrder"},
            {CoNumberRole, "coNumber"},
            {ScheduledRole, "scheduled"},
            {PreviousRole, "previous"},
            {ThisPeriodRole, "thisPeriod"},
            {StoredRole, "stored"},
            {CompletedRole, "completed"},
            {ToDateRole, "toDate"},
            {BalanceRole, "balance"},
            {RetainageRole, "retainage"},
            {PercentRole, "percent"},
            {PercentToDateRole, "percentToDate"},
            {QuantityRole, "quantity"},
            {UnitRole, "unit"},
            {UnitPriceRole, "unitPrice"},
            {InstalledRole, "installed"},
            {LockedRole, "locked"}};
}

void LineModel::setRows(const QList<BillingRow> &rows, const QSet<int> &locked) {
    bool same = rows.size() == m_rows.size();
    for (int i = 0; same && i < rows.size(); ++i)
        same = rows[i].line.id == m_rows[i].line.id;
    if (same) {
        m_rows = rows;
        m_locked = locked;
        if (!rows.isEmpty())
            emit dataChanged(index(0), index(int(rows.size()) - 1));
        return;
    }
    beginResetModel();
    m_rows = rows;
    m_locked = locked;
    endResetModel();
    emit countChanged();
}

int LineModel::indexOf(int id) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].line.id == id)
            return i;
    return -1;
}

int LineModel::idAt(int index) const { return index >= 0 && index < m_rows.size() ? m_rows[index].line.id : 0; }

QVariantMap LineModel::get(int row) const {
    QVariantMap map;
    if (row < 0 || row >= m_rows.size())
        return map;
    const auto roles = roleNames();
    for (auto it = roles.begin(); it != roles.end(); ++it)
        map.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return map;
}
