#pragma once

#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

// Money is kept in whole cents, so every total adds up exactly.
using Cents = qint64;

// Who the application is from and to, and the contract it bills against.
struct ProjectInfo {
    QString name;
    QString location;
    QString owner;
    QString contractor;
    QString architect;
    QString contractFor;
    QString contractNumber;
    QDate contractDate;
    QString currency = "$";
    // Retainage withheld on new periods, as percentages; each period keeps
    // its own copy so a reduction later on does not rewrite earlier ones.
    double retainageWork = 10;
    double retainageStored = 10;
};

enum class LineKind { Base = 0, ChangeOrder = 1 };

// One line of the schedule of values. Change orders are lines too: once
// approved they are billed like any other line, from the period they were
// approved in.
struct Line {
    int id = 0;
    int position = 0;
    int kind = int(LineKind::Base);
    QString code;
    QString description;
    Cents scheduled = 0;
    // Quantities come from a Plumb takeoff; zero when the line is a lump sum.
    double quantity = 0;
    QString unit;
    Cents unitPrice = 0;
    // Change orders only.
    int coNumber = 0;
    QDate coDate;
    bool approved = false;
    // The application number the change order was approved in.
    int approvedIn = 0;

    bool isChangeOrder() const { return kind == int(LineKind::ChangeOrder); }
    // Whether the line is part of the contract in application number n.
    bool billableIn(int number) const { return !isChangeOrder() || (approved && approvedIn <= number); }
};

// One application for payment, covering the work up to periodTo.
struct Period {
    int id = 0;
    int number = 0;
    QDate periodTo;
    QDate applicationDate;
    double retainageWork = 10;
    double retainageStored = 10;
    bool submitted = false;
};

// A line's standing at the end of a period, to date: the work completed and
// the materials stored on site but not yet installed.
struct Entry {
    Cents completed = 0;
    Cents stored = 0;
};

// The whole project, read at once; it is small enough that every
// calculation works from a full copy.
struct ProjectData {
    ProjectInfo info;
    QList<Line> lines;
    QList<Period> periods;
    QHash<QPair<int, int>, Entry> entries;

    const Period *period(int id) const;
    const Period *periodNumber(int number) const;
    const Period *previous(const Period &period) const;
    const Line *line(int id) const;
    Entry entry(int periodId, int lineId) const { return entries.value({periodId, lineId}); }
    // Once an application has gone out the original schedule of values is
    // fixed; changes to the contract go through change orders.
    bool sovLocked() const;
    // Whether a line has been billed on an application that went out.
    bool billedOnSubmitted(int lineId) const;
    int nextChangeOrderNumber() const;
};

// One project is one SQLite file: the schedule of values, change orders,
// and every period's progress, so it can be copied or backed up whole.
class Store {
public:
    Store();
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    bool create(const QString &path, const ProjectInfo &info);
    bool open(const QString &path);
    void close();
    bool isOpen() const { return !m_connection.isEmpty(); }
    QString path() const { return m_path; }
    QString error() const { return m_error; }

    ProjectData load() const;
    bool setInfo(const ProjectInfo &info);

    // Inserts the line at the end, keeping its id and position when set.
    int insertLine(const Line &line);
    bool updateLine(const Line &line);
    bool removeLine(int id);
    bool moveLine(int id, int position);

    int insertPeriod(const Period &period);
    bool updatePeriod(const Period &period);
    bool removePeriod(int id);

    bool setEntry(int periodId, int lineId, const Entry &entry);

    // Several changes that land together, or not at all.
    bool begin();
    bool commit();
    void rollback();

    // The whole project as JSON, for undo, and putting it back.
    QJsonObject snapshot() const;
    bool restore(const QJsonObject &snapshot);

private:
    bool connect(const QString &path);
    bool exec(const QString &statement) const;
    bool fail(const QString &message) const;

    QString m_connection;
    QString m_path;
    mutable QString m_error;
};
