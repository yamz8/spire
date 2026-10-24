#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include "store.h"

// One item of a Plumb takeoff: a measured quantity, and optionally its unit
// price and how much of it is installed so far. Rows measured on several
// sheets are added together.
struct TakeoffRow {
    QString code;
    QString description;
    QString unit;
    double quantity = 0;
    double installed = 0;
    bool hasInstalled = false;
    Cents unitPrice = 0;
    bool hasUnitPrice = false;
    Cents amount = 0;
    bool hasAmount = false;
    int sourceLine = 0;
};

struct Takeoff {
    QList<TakeoffRow> rows;
    // Rows that could not be read, said in words.
    QStringList warnings;
    QString error;
    bool hasInstalled = false;
};

// Reads a CSV exported from Plumb. Columns are found by their headings
// (Code, Description, Quantity, Unit, Unit price, Amount, Installed and the
// usual variants), in any order; commas, semicolons, or tabs separate them.
Takeoff parseTakeoff(const QByteArray &csv);
Takeoff readTakeoff(const QString &path);

// What importing a takeoff would do to each line, worked out before anything
// changes so the person can look first.
struct ImportChange {
    // Same: the line is already as the takeoff has it.
    enum Action { Update, Add, Skip, Same };
    Action action = Skip;
    TakeoffRow row;
    // The line as it would be after the import (for Add, without an id).
    Line line;
    bool scheduleChanged = false;
    // Progress this period from the installed quantity, when there is one.
    bool billing = false;
    Cents completed = 0;
    double percent = 0;
    QString note;
};

struct ImportPlan {
    QList<ImportChange> changes;
    int updates = 0;
    int additions = 0;
    int skipped = 0;
    int unchanged = 0;
    int billed = 0;
};

// Lines are matched by code, then by description, ignoring case. A locked
// schedule of values keeps its amounts; new items then need a change order.
// Installed quantities bill the period when it is still a draft.
ImportPlan planImport(const ProjectData &data, const Takeoff &takeoff, int periodId);

// How a file writes its decimals: 1,234.5 or 1.234,5. Auto decides per
// number, which is right unless a number is ambiguous on its own (1.240).
enum class Decimals { Auto, Point, Comma };

// Reads a number the way spreadsheets write it: 1,234.50, 1.234,50, $1 234,
// or (12.00) for a negative. False when it is not a number.
bool parseNumber(const QString &text, double *value, Decimals decimals = Decimals::Auto);
