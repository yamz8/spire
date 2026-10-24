#pragma once

#include <QAbstractListModel>
#include <QSet>

#include "billing.h"

// The continuation sheet's rows for the period on screen, as a list QML can
// show. Amounts are whole cents; QML formats them through the backend.
class LineModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NumberRole,
        CodeRole,
        DescriptionRole,
        ChangeOrderRole,
        CoNumberRole,
        ScheduledRole,
        PreviousRole,
        ThisPeriodRole,
        StoredRole,
        CompletedRole,
        ToDateRole,
        BalanceRole,
        RetainageRole,
        PercentRole,
        PercentToDateRole,
        QuantityRole,
        UnitRole,
        UnitPriceRole,
        InstalledRole,
        LockedRole,
    };

    explicit LineModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    // Replaces the rows; when the same lines are still there in the same
    // order the views keep their place and only the values change.
    void setRows(const QList<BillingRow> &rows, const QSet<int> &locked);
    const QList<BillingRow> &rows() const { return m_rows; }
    Q_INVOKABLE int indexOf(int id) const;
    Q_INVOKABLE int idAt(int index) const;
    // A row as a map of its roles, for rows the view has not built.
    Q_INVOKABLE QVariantMap get(int index) const;

signals:
    void countChanged();

private:
    QList<BillingRow> m_rows;
    QSet<int> m_locked;
};
