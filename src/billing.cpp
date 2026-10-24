#include "billing.h"

#include <cmath>

Cents percentOf(Cents amount, double percent) { return std::llround(double(amount) * percent / 100.0); }

double percentage(Cents part, Cents whole) { return whole ? 100.0 * double(part) / double(whole) : 0; }

Billing computeBilling(const ProjectData &data, int periodId) {
    const Period *period = data.period(periodId);
    return period ? computeBilling(data, *period) : Billing{};
}

Billing computeBilling(const ProjectData &data, const Period &current) {
    Billing billing;
    const Period *period = &current;
    billing.period = current;
    const Period *previous = data.previous(current);
    BillingSummary &sum = billing.summary;

    for (const auto &line : data.lines) {
        if (line.isChangeOrder() && line.approved) {
            Cents &additions = line.approvedIn < period->number ? sum.coPreviousAdditions : sum.coThisAdditions;
            Cents &deductions = line.approvedIn < period->number ? sum.coPreviousDeductions : sum.coThisDeductions;
            if (line.approvedIn <= period->number)
                (line.scheduled >= 0 ? additions : deductions) += line.scheduled;
        }
        if (!line.billableIn(period->number))
            continue;

        BillingRow row;
        row.line = line;
        row.scheduled = line.scheduled;
        const Entry now = data.entry(period->id, line.id);
        const Entry before = previous ? data.entry(previous->id, line.id) : Entry{};
        row.previous = before.completed;
        row.previousStored = before.stored;
        row.completed = now.completed;
        row.thisPeriod = now.completed - before.completed;
        row.stored = now.stored;
        row.toDate = now.completed + now.stored;
        row.balance = row.scheduled - row.toDate;
        row.retainage = percentOf(now.completed, period->retainageWork) + percentOf(now.stored, period->retainageStored);
        row.percentComplete = percentage(row.completed, row.scheduled);
        row.percentToDate = percentage(row.toDate, row.scheduled);
        billing.rows << row;

        (line.isChangeOrder() ? sum.changeOrders : sum.originalContract) += line.scheduled;
        sum.scheduledTotal += row.scheduled;
        sum.previousTotal += row.previous;
        sum.thisPeriodTotal += row.thisPeriod;
        sum.completedWork += row.completed;
        sum.storedMaterials += row.stored;
        sum.completedToDate += row.toDate;
        sum.balanceTotal += row.balance;
        sum.retainageWork += percentOf(now.completed, period->retainageWork);
        sum.retainageStored += percentOf(now.stored, period->retainageStored);
    }

    sum.contractToDate = sum.originalContract + sum.changeOrders;
    sum.retainage = sum.retainageWork + sum.retainageStored;
    sum.earnedLessRetainage = sum.completedToDate - sum.retainage;
    // What was certified before is what the previous application earned
    // after its own retainage, so a change of rate shows up as a release.
    if (previous)
        sum.previousCertificates = computeBilling(data, previous->id).summary.earnedLessRetainage;
    sum.currentDue = sum.earnedLessRetainage - sum.previousCertificates;
    sum.balanceToFinish = sum.contractToDate - sum.earnedLessRetainage;
    sum.percentComplete = percentage(sum.completedToDate, sum.contractToDate);
    return billing;
}
