#pragma once

#include "store.h"

// One line of the continuation sheet (G703) for one period. The letters are
// the sheet's columns.
struct BillingRow {
    Line line;
    Cents scheduled = 0;    // C
    Cents previous = 0;     // D: work completed on earlier applications
    Cents thisPeriod = 0;   // E: work completed this period
    Cents stored = 0;       // F: materials presently stored, not in D or E
    Cents completed = 0;    // D + E
    Cents toDate = 0;       // G: D + E + F
    Cents balance = 0;      // H: C - G
    Cents retainage = 0;    // I
    Cents previousStored = 0;
    // Work completed to date as a share of the scheduled value, 0 to 100,
    // and the sheet's own % column, which counts stored materials too.
    double percentComplete = 0;
    double percentToDate = 0;
};

// The application and certificate for payment (G702), with the change order
// summary, for one period.
struct BillingSummary {
    Cents originalContract = 0;   // 1
    Cents changeOrders = 0;       // 2
    Cents contractToDate = 0;     // 3
    Cents completedToDate = 0;    // 4: G
    Cents completedWork = 0;      // D + E
    Cents storedMaterials = 0;    // F
    Cents retainageWork = 0;      // 5a
    Cents retainageStored = 0;    // 5b
    Cents retainage = 0;          // 5
    Cents earnedLessRetainage = 0; // 6
    Cents previousCertificates = 0; // 7
    Cents currentDue = 0;         // 8
    Cents balanceToFinish = 0;    // 9: 3 - 6
    Cents scheduledTotal = 0;
    Cents previousTotal = 0;
    Cents thisPeriodTotal = 0;
    Cents balanceTotal = 0;       // H total, without retainage
    // Change orders approved on earlier applications, and on this one.
    Cents coPreviousAdditions = 0;
    Cents coPreviousDeductions = 0;
    Cents coThisAdditions = 0;
    Cents coThisDeductions = 0;
    double percentComplete = 0;
};

struct Billing {
    Period period;
    QList<BillingRow> rows;
    BillingSummary summary;
};

// Everything on an application follows from the to-date entries: what was
// billed before is the previous period's to date, and this period is the
// difference. Rounding happens per line, so the sheet's columns add up to
// the certificate's lines exactly.
Billing computeBilling(const ProjectData &data, int periodId);
// The same for a period that need not be saved yet, such as the first one
// before it is started.
Billing computeBilling(const ProjectData &data, const Period &period);

Cents percentOf(Cents amount, double percent);
double percentage(Cents part, Cents whole);
