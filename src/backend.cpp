#include "backend.h"

#include "billing.h"
#include "filepicker.h"
#include "money.h"
#include "payapp.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <cmath>

namespace {
constexpr int UndoLimit = 200;

FileFilter projectFilter() { return {Backend::tr("Spire project"), {"*.spire"}, {"application/x-spire"}}; }

QDate toDate(const QVariant &value) {
    if (value.typeId() == QMetaType::QDate)
        return value.toDate();
    if (value.typeId() == QMetaType::QDateTime)
        return value.toDateTime().date();
    return QDate::fromString(value.toString(), Qt::ISODate);
}

QDate endOfMonth(const QDate &date) { return QDate(date.year(), date.month(), date.daysInMonth()); }

// Whether a line's amount (and with it quantity and unit price) can still
// change: the original schedule until an application goes out, a change
// order until it is billed on one or its approving application goes out.
bool amountFixed(const ProjectData &data, const Line &line) {
    if (!line.isChangeOrder())
        return data.sovLocked();
    if (data.billedOnSubmitted(line.id))
        return true;
    const Period *approvedIn = line.approved ? data.periodNumber(line.approvedIn) : nullptr;
    return approvedIn && approvedIn->submitted;
}
}

Backend::Backend(FilePicker *picker, QObject *parent) : QObject(parent), m_picker(picker) {
    connect(m_picker, &FilePicker::selected, this, &Backend::pickerSelected);
    connect(m_picker, &FilePicker::failed, this, &Backend::setError);
}

Backend::~Backend() = default;

QVariantMap Backend::info() const {
    const ProjectInfo &i = m_data.info;
    return {{"name", i.name},
            {"location", i.location},
            {"owner", i.owner},
            {"contractor", i.contractor},
            {"architect", i.architect},
            {"contractFor", i.contractFor},
            {"contractNumber", i.contractNumber},
            {"contractDate", i.contractDate},
            {"currency", i.currency},
            {"retainageWork", i.retainageWork},
            {"retainageStored", i.retainageStored}};
}

QVariantList Backend::periods() const {
    QVariantList list;
    for (const auto &p : m_data.periods)
        list << QVariantMap{{"id", p.id}, {"number", p.number}, {"periodTo", p.periodTo}, {"submitted", p.submitted}};
    return list;
}

void Backend::setCurrentPeriod(int id) {
    if (id == m_currentPeriod || !m_data.period(id))
        return;
    showPeriod(id);
}

void Backend::showPeriod(int id) {
    m_currentPeriod = id;
    emit currentPeriodChanged();
    reload();
}

void Backend::stepPeriod(int step) {
    for (int i = 0; i < m_data.periods.size(); ++i)
        if (m_data.periods[i].id == m_currentPeriod) {
            const int next = std::clamp(i + step, 0, int(m_data.periods.size()) - 1);
            setCurrentPeriod(m_data.periods[next].id);
            return;
        }
}

QVariantMap Backend::period() const {
    const Period *p = current();
    if (!p)
        return {};
    const bool latest = p->id == m_data.periods.last().id;
    return {{"id", p->id},
            {"number", p->number},
            {"periodTo", p->periodTo},
            {"applicationDate", p->applicationDate},
            {"retainageWork", p->retainageWork},
            {"retainageStored", p->retainageStored},
            {"submitted", p->submitted},
            {"latest", latest},
            {"canReopen", latest && p->submitted},
            {"canRemove", latest && !p->submitted}};
}

bool Backend::editable() const { return current() && !current()->submitted; }

bool Backend::canStartPeriod() const {
    return isOpen() && (m_data.periods.isEmpty() || m_data.periods.last().submitted);
}

QVariantList Backend::changeOrders() const {
    QVariantList list;
    for (const auto &line : m_data.lines) {
        if (!line.isChangeOrder())
            continue;
        const Period *approvedIn = line.approved ? m_data.periodNumber(line.approvedIn) : nullptr;
        const bool fixed = amountFixed(m_data, line);
        list << QVariantMap{{"id", line.id},
                            {"number", line.coNumber},
                            {"description", line.description},
                            {"amount", double(line.scheduled)},
                            {"date", line.coDate},
                            {"approved", line.approved},
                            {"approvedIn", line.approvedIn},
                            {"locked", fixed},
                            {"canUnapprove", line.approved && !(approvedIn && approvedIn->submitted) &&
                                                 !m_data.billedOnSubmitted(line.id)},
                            {"canRemove", !fixed}};
    }
    return list;
}

int Backend::pendingChangeOrders() const {
    return int(std::count_if(m_data.lines.begin(), m_data.lines.end(),
                             [](const Line &l) { return l.isChangeOrder() && !l.approved; }));
}

bool Backend::hasQuantities() const {
    for (const auto &row : m_lines.rows())
        if (row.line.quantity > 0)
            return true;
    return false;
}

void Backend::reload() {
    m_data = m_store.isOpen() ? m_store.load() : ProjectData{};
    if (!m_data.period(m_currentPeriod)) {
        m_currentPeriod = m_data.periods.isEmpty() ? 0 : m_data.periods.last().id;
        emit currentPeriodChanged();
    }
    // Before the first application is started the sheet still shows the
    // schedule of values, as it will be billed on application 1.
    Period first;
    first.number = 1;
    first.retainageWork = m_data.info.retainageWork;
    first.retainageStored = m_data.info.retainageStored;
    const Billing billing = current() ? computeBilling(m_data, *current()) : computeBilling(m_data, first);

    QSet<int> locked;
    for (const auto &line : m_data.lines)
        if (amountFixed(m_data, line))
            locked.insert(line.id);
    m_lines.setRows(billing.rows, locked);

    const BillingSummary &s = billing.summary;
    m_summary = {{"originalContract", double(s.originalContract)},
                 {"changeOrders", double(s.changeOrders)},
                 {"contractToDate", double(s.contractToDate)},
                 {"completedToDate", double(s.completedToDate)},
                 {"completedWork", double(s.completedWork)},
                 {"storedMaterials", double(s.storedMaterials)},
                 {"retainageWork", double(s.retainageWork)},
                 {"retainageStored", double(s.retainageStored)},
                 {"retainage", double(s.retainage)},
                 {"earnedLessRetainage", double(s.earnedLessRetainage)},
                 {"previousCertificates", double(s.previousCertificates)},
                 {"currentDue", double(s.currentDue)},
                 {"balanceToFinish", double(s.balanceToFinish)},
                 {"scheduledTotal", double(s.scheduledTotal)},
                 {"previousTotal", double(s.previousTotal)},
                 {"thisPeriodTotal", double(s.thisPeriodTotal)},
                 {"balanceTotal", double(s.balanceTotal)},
                 {"coPreviousAdditions", double(s.coPreviousAdditions)},
                 {"coPreviousDeductions", double(s.coPreviousDeductions)},
                 {"coThisAdditions", double(s.coThisAdditions)},
                 {"coThisDeductions", double(s.coThisDeductions)},
                 {"percentComplete", s.percentComplete}};

    if (m_selectedLine && m_lines.indexOf(m_selectedLine) < 0 && !m_data.line(m_selectedLine)) {
        m_selectedLine = 0;
        emit selectionChanged();
    }
    emit dataChanged();
}

int Backend::baseLineCount() const {
    return int(std::count_if(m_data.lines.begin(), m_data.lines.end(), [](const Line &l) { return !l.isChangeOrder(); }));
}

bool Backend::reject(const QString &message) {
    setError(message);
    return false;
}

bool Backend::change(const std::function<bool()> &work) {
    if (!m_store.isOpen())
        return reject(tr("Open a project first."));
    const QJsonObject before = m_store.snapshot();
    if (!m_store.begin())
        return reject(m_store.error());
    m_error.clear();
    if (!work()) {
        m_store.rollback();
        if (m_error.isEmpty())
            setError(m_store.error());
        else
            emit errorChanged();
        reload();
        return false;
    }
    if (!m_store.commit()) {
        m_store.rollback();
        reload();
        return reject(m_store.error());
    }
    const QJsonObject after = m_store.snapshot();
    if (after != before) {
        m_undo.append({before, after, m_currentPeriod});
        while (m_undo.size() > UndoLimit)
            m_undo.removeFirst();
        m_redo.clear();
        emit historyChanged();
    }
    clearError();
    reload();
    return true;
}

void Backend::undo() {
    if (m_undo.isEmpty())
        return;
    const Change step = m_undo.takeLast();
    if (!m_store.restore(step.before)) {
        m_undo.append(step);
        setError(m_store.error());
        return;
    }
    m_redo.append(step);
    emit historyChanged();
    m_currentPeriod = step.period;
    emit currentPeriodChanged();
    reload();
}

void Backend::redo() {
    if (m_redo.isEmpty())
        return;
    const Change step = m_redo.takeLast();
    if (!m_store.restore(step.after)) {
        m_redo.append(step);
        setError(m_store.error());
        return;
    }
    m_undo.append(step);
    emit historyChanged();
    m_currentPeriod = step.period;
    emit currentPeriodChanged();
    reload();
}

// Projects.

QString Backend::folder() const {
    const QString remembered = QSettings().value("folder").toString();
    if (!remembered.isEmpty() && QDir(remembered).exists())
        return remembered;
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(documents).exists() ? documents : QDir::homePath();
}

void Backend::rememberFolder(const QString &path) {
    if (m_remember)
        QSettings().setValue("folder", QFileInfo(path).absolutePath());
}

void Backend::newProject() { m_picker->save("new", tr("New Project"), folder() + "/" + tr("Project") + ".spire", projectFilter()); }

void Backend::openProject() { m_picker->open("open", tr("Open Project"), {projectFilter()}, false, folder()); }

void Backend::importTakeoff() {
    if (!isOpen())
        return;
    m_picker->open("takeoff", tr("Import Plumb Takeoff"), {{tr("Plumb CSV export"), {"*.csv", "*.CSV", "*.tsv", "*.txt"}, {"text/csv"}}},
                   false, folder());
}

void Backend::exportPayApplication() {
    if (!isOpen())
        return;
    if (!current()) {
        setError(tr("Start an application before exporting it."));
        return;
    }
    const QString name = tr("%1 – Application %2").arg(m_data.info.name, QLocale().toString(current()->number));
    QString safe = name;
    safe.replace('/', '-');
    m_picker->save("export", tr("Export Pay Application"), folder() + "/" + safe + ".pdf",
                   {tr("PDF document"), {"*.pdf"}, {"application/pdf"}});
}

void Backend::pickerSelected(const QString &purpose, const QList<QUrl> &urls) {
    if (urls.isEmpty())
        return;
    const QString path = urls.first().toLocalFile();
    if (purpose == "new")
        createProjectAt(path);
    else if (purpose == "open")
        openProjectAt(path);
    else if (purpose == "takeoff")
        importTakeoffFrom(path);
    else if (purpose == "export")
        exportPayApplicationTo(path);
}

void Backend::closeProject() {
    m_store.close();
    m_undo.clear();
    m_redo.clear();
    m_currentPeriod = 0;
    m_selectedLine = 0;
    cancelImport();
    emit historyChanged();
    emit selectionChanged();
}

bool Backend::createProjectAt(const QString &chosen, bool remember) {
    QString path = chosen;
    if (!path.endsWith(".spire", Qt::CaseInsensitive))
        path += ".spire";
    ProjectInfo info;
    info.name = QFileInfo(path).completeBaseName();
    const QString symbol = QLocale().currencySymbol(QLocale::CurrencySymbol);
    info.currency = symbol.isEmpty() ? "$" : symbol;
    // A fresh project replaces the open one only once it exists.
    Store probe;
    if (!probe.create(path, info))
        return reject(probe.error());
    probe.close();
    return openProjectAt(path, remember);
}

bool Backend::openProjectAt(const QString &path, bool remember) {
    Store probe;
    if (!probe.open(path))
        return reject(probe.error());
    probe.close();
    closeProject();
    if (!m_store.open(path))
        return reject(m_store.error());
    m_remember = remember;
    if (remember) {
        rememberFolder(path);
        QSettings().setValue("lastProject", m_store.path());
    }
    clearError();
    emit projectChanged();
    reload();
    return true;
}

void Backend::reopenLastProject() {
    const QString last = QSettings().value("lastProject").toString();
    if (!last.isEmpty() && QFileInfo(last).isFile())
        openProjectAt(last);
}

bool Backend::setInfo(const QString &field, const QVariant &value) {
    ProjectInfo info = m_data.info;
    const QString text = value.toString().trimmed();
    if (field == "name") {
        if (text.isEmpty())
            return reject(tr("The project needs a name."));
        info.name = text;
    } else if (field == "location") info.location = text;
    else if (field == "owner") info.owner = text;
    else if (field == "contractor") info.contractor = text;
    else if (field == "architect") info.architect = text;
    else if (field == "contractFor") info.contractFor = text;
    else if (field == "contractNumber") info.contractNumber = text;
    else if (field == "contractDate") info.contractDate = toDate(value);
    else if (field == "currency") info.currency = text;
    else if (field == "retainageWork" || field == "retainageStored") {
        double percent = 0;
        if (!parseNumber(text, &percent) || percent < 0 || percent > 100)
            return reject(tr("Retainage is a percentage from 0 to 100."));
        (field == "retainageWork" ? info.retainageWork : info.retainageStored) = percent;
    } else {
        return false;
    }
    return change([&] { return m_store.setInfo(info); });
}

// The schedule of values.

int Backend::addLine() {
    if (m_data.sovLocked()) {
        reject(tr("The schedule of values is fixed once an application is submitted. Add a change order instead."));
        return 0;
    }
    int id = 0;
    change([&] {
        // New lines go before the change orders, which always come last.
        id = m_store.insertLine(Line{});
        return id && m_store.moveLine(id, baseLineCount() + 1);
    });
    if (id) {
        selectLine(id);
        emit lineAdded(id);
    }
    return id;
}

bool Backend::setLineField(int id, const QString &field, const QString &value) {
    const Line *found = m_data.line(id);
    if (!found)
        return false;
    Line line = *found;
    const QString text = value.trimmed();
    const bool fixed = amountFixed(m_data, line);
    const Cents oldScheduled = line.scheduled;

    if (field == "code") {
        line.code = text;
    } else if (field == "description") {
        line.description = text;
    } else if (field == "unit") {
        line.unit = text;
    } else if (field == "scheduled" || field == "quantity" || field == "unitPrice") {
        if (fixed)
            return reject(line.isChangeOrder()
                              ? tr("This change order has been billed on a submitted application; its amount is fixed.")
                              : tr("The schedule of values is fixed once an application is submitted. Add a change order instead."));
        if (field == "quantity") {
            double quantity = 0;
            if (!text.isEmpty() && (!parseNumber(text, &quantity) || quantity < 0))
                return reject(tr("“%1” is not a quantity.").arg(text));
            line.quantity = quantity;
        } else {
            Cents amount = 0;
            if (!text.isEmpty() && !parseMoney(text, &amount))
                return reject(tr("“%1” is not an amount.").arg(text));
            if (field == "unitPrice")
                line.unitPrice = amount;
            else {
                line.scheduled = amount;
                // A scheduled value typed by hand makes the line a lump sum.
                if (line.unitPrice && line.quantity && std::llround(line.quantity * double(line.unitPrice)) != amount)
                    line.unitPrice = 0;
            }
        }
        if (field != "scheduled" && line.unitPrice && line.quantity > 0)
            line.scheduled = std::llround(line.quantity * double(line.unitPrice));
    } else {
        return false;
    }
    if (line.code == found->code && line.description == found->description && line.unit == found->unit &&
        line.scheduled == found->scheduled && line.quantity == found->quantity && line.unitPrice == found->unitPrice)
        return true;

    return change([&] {
        if (!m_store.updateLine(line))
            return false;
        // A new scheduled value keeps each draft's percent complete.
        if (line.scheduled != oldScheduled)
            for (const auto &period : m_data.periods) {
                if (period.submitted)
                    continue;
                Entry entry = m_data.entry(period.id, id);
                if (!entry.completed && !entry.stored)
                    continue;
                entry.completed = percentOf(line.scheduled, percentage(entry.completed, oldScheduled));
                if (std::llabs(entry.completed + entry.stored) > std::llabs(line.scheduled))
                    entry.stored = std::max<Cents>(0, line.scheduled - entry.completed);
                if (!m_store.setEntry(period.id, id, entry))
                    return false;
            }
        return true;
    });
}

bool Backend::removeLine(int id) {
    const Line *line = m_data.line(id);
    if (!line)
        return false;
    if (amountFixed(m_data, *line))
        return reject(line->isChangeOrder()
                          ? tr("This change order is on a submitted application, so it stays.")
                          : tr("The schedule of values is fixed once an application is submitted."));
    const int index = m_lines.indexOf(id);
    if (!change([&] { return m_store.removeLine(id); }))
        return false;
    if (m_selectedLine == id || !m_selectedLine)
        selectLine(m_lines.idAt(std::min(index, m_lines.count() - 1)));
    setNotice(tr("Deleted “%1”. Ctrl+Z brings it back.").arg(line->description.isEmpty() ? tr("the line") : line->description));
    return true;
}

bool Backend::moveLine(int id, int step) {
    const Line *line = m_data.line(id);
    if (!line)
        return false;
    // Lines move among their own kind: the original schedule first, then the
    // change orders in their order.
    QList<int> ids;
    for (const auto &l : m_data.lines)
        ids << l.id;
    const int from = int(ids.indexOf(id)), to = from + step;
    if (to < 0 || to >= ids.size() || m_data.line(ids[to])->kind != line->kind)
        return false;
    return change([&] { return m_store.moveLine(id, to + 1); });
}

// Billing.

bool Backend::setEntry(int lineId, Entry entry) {
    const Period *period = current();
    if (!period)
        return reject(tr("Start an application to bill work."));
    if (period->submitted)
        return reject(tr("Application %1 was submitted. Reopen it to change it.").arg(QLocale().toString(period->number)));
    const Line *line = m_data.line(lineId);
    if (!line || !line->billableIn(period->number))
        return reject(tr("That line is not part of this application."));
    const Cents scheduled = line->scheduled;
    const int sign = scheduled < 0 ? -1 : 1;
    if (sign * entry.completed < 0 || sign * entry.stored < 0)
        return reject(tr("Completed work and stored materials cannot go below zero."));
    if (sign * (entry.completed + entry.stored) > sign * scheduled)
        return reject(tr("That would bill %1 of %2 scheduled. Completed work and stored materials cannot be more than the scheduled value.")
                          .arg(money(double(entry.completed + entry.stored)), money(double(scheduled))));
    const Entry old = m_data.entry(period->id, lineId);
    if (old.completed == entry.completed && old.stored == entry.stored)
        return true;
    return change([&] { return m_store.setEntry(period->id, lineId, entry); });
}

bool Backend::setPercent(int lineId, const QString &text) {
    const Line *line = m_data.line(lineId);
    double value = 0;
    if (!line)
        return false;
    if (!parseNumber(text.isEmpty() ? "0" : text, &value) || value < 0 || value > 100)
        return reject(tr("Percent complete is a number from 0 to 100."));
    Entry entry = current() ? m_data.entry(current()->id, lineId) : Entry{};
    entry.completed = percentOf(line->scheduled, value);
    // Stored materials that are now installed leave the stored column.
    if (std::llabs(entry.completed + entry.stored) > std::llabs(line->scheduled))
        entry.stored = line->scheduled - entry.completed;
    return setEntry(lineId, entry);
}

bool Backend::setThisPeriod(int lineId, const QString &text) {
    Cents amount = 0;
    if (!text.trimmed().isEmpty() && !parseMoney(text, &amount))
        return reject(tr("“%1” is not an amount.").arg(text.trimmed()));
    const Period *period = current();
    if (!period)
        return reject(tr("Start an application to bill work."));
    const Period *previous = m_data.previous(*period);
    Entry entry = m_data.entry(period->id, lineId);
    entry.completed = (previous ? m_data.entry(previous->id, lineId).completed : 0) + amount;
    return setEntry(lineId, entry);
}

bool Backend::setStored(int lineId, const QString &text) {
    Cents amount = 0;
    if (!text.trimmed().isEmpty() && !parseMoney(text, &amount))
        return reject(tr("“%1” is not an amount.").arg(text.trimmed()));
    Entry entry = current() ? m_data.entry(current()->id, lineId) : Entry{};
    entry.stored = amount;
    return setEntry(lineId, entry);
}

bool Backend::setInstalled(int lineId, const QString &text) {
    const Line *line = m_data.line(lineId);
    if (!line)
        return false;
    if (line->quantity <= 0)
        return reject(tr("This line has no quantity to measure against."));
    double installed = 0;
    if (!text.trimmed().isEmpty() && (!parseNumber(text, &installed) || installed < 0))
        return reject(tr("“%1” is not a quantity.").arg(text.trimmed()));
    if (installed > line->quantity)
        return reject(tr("Only %1 %2 are on the schedule.").arg(quantity(line->quantity), line->unit));
    return setPercent(lineId, QString::number(installed / line->quantity * 100, 'g', 17));
}

bool Backend::completeLine(int lineId) {
    const Line *line = m_data.line(lineId);
    if (!line)
        return false;
    return setEntry(lineId, {line->scheduled, 0});
}

// Applications.

bool Backend::startPeriod() {
    if (!isOpen())
        return false;
    if (!canStartPeriod())
        return reject(tr("Submit application %1 before starting the next one.")
                          .arg(QLocale().toString(m_data.periods.last().number)));
    const Period *last = m_data.periods.isEmpty() ? nullptr : &m_data.periods.last();
    Period period;
    period.number = last ? last->number + 1 : 1;
    const QDate today = QDate::currentDate();
    period.periodTo = last && last->periodTo.isValid() ? endOfMonth(last->periodTo.addMonths(1)) : endOfMonth(today);
    period.applicationDate = period.periodTo;
    period.retainageWork = last ? last->retainageWork : m_data.info.retainageWork;
    period.retainageStored = last ? last->retainageStored : m_data.info.retainageStored;
    int id = 0;
    const bool ok = change([&] {
        id = m_store.insertPeriod(period);
        if (!id)
            return false;
        // Work to date and materials still stored carry over; this period
        // starts at nothing.
        if (last)
            for (const auto &line : m_data.lines) {
                const Entry entry = m_data.entry(last->id, line.id);
                if ((entry.completed || entry.stored) && !m_store.setEntry(id, line.id, entry))
                    return false;
            }
        return true;
    });
    if (ok) {
        showPeriod(id);
        // The undo step returns to the new application, not the old one.
        m_undo.last().period = id;
        setNotice(tr("Started application %1.").arg(QLocale().toString(period.number)));
    }
    return ok;
}

bool Backend::setPeriodField(const QString &field, const QVariant &value) {
    const Period *found = current();
    if (!found)
        return false;
    if (found->submitted)
        return reject(tr("Application %1 was submitted. Reopen it to change it.").arg(QLocale().toString(found->number)));
    Period period = *found;
    if (field == "periodTo" || field == "applicationDate") {
        const QDate date = toDate(value);
        if (!date.isValid())
            return reject(tr("That is not a date."));
        (field == "periodTo" ? period.periodTo : period.applicationDate) = date;
    } else if (field == "retainageWork" || field == "retainageStored") {
        double percent = 0;
        if (!parseNumber(value.toString(), &percent) || percent < 0 || percent > 100)
            return reject(tr("Retainage is a percentage from 0 to 100."));
        (field == "retainageWork" ? period.retainageWork : period.retainageStored) = percent;
    } else {
        return false;
    }
    return change([&] { return m_store.updatePeriod(period); });
}

bool Backend::submitPeriod() {
    const Period *found = current();
    if (!found || found->submitted)
        return false;
    Period period = *found;
    period.submitted = true;
    if (!change([&] { return m_store.updatePeriod(period); }))
        return false;
    setNotice(tr("Application %1 is submitted and its amounts are fixed.").arg(QLocale().toString(period.number)));
    return true;
}

bool Backend::reopenPeriod() {
    const Period *found = current();
    if (!found || !found->submitted)
        return false;
    if (found->id != m_data.periods.last().id)
        return reject(tr("Only the latest application can be reopened."));
    Period period = *found;
    period.submitted = false;
    return change([&] { return m_store.updatePeriod(period); });
}

bool Backend::removePeriod() {
    const Period *found = current();
    if (!found)
        return false;
    if (found->submitted || found->id != m_data.periods.last().id)
        return reject(tr("Only the latest application, while it is a draft, can be deleted."));
    const int number = found->number, id = found->id;
    if (!change([&] {
            // Change orders approved on it go back to waiting for approval.
            for (const auto &line : m_data.lines)
                if (line.isChangeOrder() && line.approved && line.approvedIn == number) {
                    Line pending = line;
                    pending.approved = false;
                    pending.approvedIn = 0;
                    if (!m_store.updateLine(pending))
                        return false;
                }
            return m_store.removePeriod(id);
        }))
        return false;
    setNotice(tr("Deleted application %1. Ctrl+Z brings it back.").arg(QLocale().toString(number)));
    return true;
}

// Change orders.

int Backend::addChangeOrder() {
    int id = 0;
    change([&] {
        Line line;
        line.kind = int(LineKind::ChangeOrder);
        line.coNumber = m_data.nextChangeOrderNumber();
        line.coDate = QDate::currentDate();
        id = m_store.insertLine(line);
        return id != 0;
    });
    if (id)
        emit changeOrderAdded(id);
    return id;
}

bool Backend::setChangeOrderField(int id, const QString &field, const QVariant &value) {
    const Line *found = m_data.line(id);
    if (!found || !found->isChangeOrder())
        return false;
    if (field == "amount")
        return setLineField(id, "scheduled", value.toString());
    if (field == "description")
        return setLineField(id, "description", value.toString());
    if (field == "date") {
        Line line = *found;
        line.coDate = toDate(value);
        if (line.coDate == found->coDate)
            return true;
        return change([&] { return m_store.updateLine(line); });
    }
    return false;
}

bool Backend::setApproved(int id, bool approved) {
    const Line *found = m_data.line(id);
    if (!found || !found->isChangeOrder() || found->approved == approved)
        return false;
    Line line = *found;
    if (approved) {
        // Approved on the application being worked on: the open draft, or
        // the next one when the latest has gone out.
        const Period *last = m_data.periods.isEmpty() ? nullptr : &m_data.periods.last();
        line.approved = true;
        line.approvedIn = !last ? 1 : last->submitted ? last->number + 1 : last->number;
    } else {
        const Period *approvedIn = m_data.periodNumber(line.approvedIn);
        if ((approvedIn && approvedIn->submitted) || m_data.billedOnSubmitted(id))
            return reject(tr("Change order %1 was approved on a submitted application.").arg(QLocale().toString(line.coNumber)));
        line.approved = false;
        line.approvedIn = 0;
    }
    return change([&] {
        if (!m_store.updateLine(line))
            return false;
        if (!approved)
            for (const auto &period : m_data.periods)
                if (!period.submitted && !m_store.setEntry(period.id, id, {}))
                    return false;
        return true;
    });
}

// Plumb takeoffs.

bool Backend::importTakeoffFrom(const QString &path) {
    if (!isOpen())
        return false;
    m_takeoff = readTakeoff(path);
    if (!m_takeoff.error.isEmpty()) {
        cancelImport();
        return reject(tr("Could not import %1: %2").arg(QFileInfo(path).fileName(), m_takeoff.error));
    }
    m_takeoffPath = path;
    const ImportPlan plan = planImport(m_data, m_takeoff, m_currentPeriod);
    QVariantList rows;
    for (const auto &c : plan.changes)
        rows << QVariantMap{{"action", int(c.action)},
                            {"code", c.row.code},
                            {"description", c.row.description.isEmpty() ? c.line.description : c.row.description},
                            {"quantity", c.row.quantity},
                            {"unit", c.row.unit},
                            {"scheduled", double(c.line.scheduled)},
                            {"scheduleChanged", c.scheduleChanged},
                            {"billing", c.billing},
                            {"percent", c.percent},
                            {"note", c.note}};
    m_importPreview = {{"file", QFileInfo(path).fileName()},
                       {"rows", rows},
                       {"updates", plan.updates},
                       {"additions", plan.additions},
                       {"skipped", plan.skipped},
                       {"unchanged", plan.unchanged},
                       {"billed", plan.billed},
                       {"warnings", m_takeoff.warnings},
                       {"period", current() ? current()->number : 0}};
    rememberFolder(path);
    emit importPreviewChanged();
    return true;
}

void Backend::cancelImport() {
    m_takeoff = {};
    m_takeoffPath.clear();
    if (!m_importPreview.isEmpty()) {
        m_importPreview.clear();
        emit importPreviewChanged();
    }
}

bool Backend::applyImport() {
    if (m_takeoffPath.isEmpty())
        return false;
    // Worked out again, in case the project changed while the preview was up.
    const ImportPlan plan = planImport(m_data, m_takeoff, m_currentPeriod);
    const QString file = QFileInfo(m_takeoffPath).fileName();
    const int periodId = m_currentPeriod;
    int added = 0;
    const bool ok = change([&] {
        for (const auto &c : plan.changes) {
            int id = c.line.id;
            if (c.action == ImportChange::Skip || c.action == ImportChange::Same)
                continue;
            if (c.action == ImportChange::Add) {
                Line line = c.line;
                line.position = 0;
                id = m_store.insertLine(line);
                if (!id || !m_store.moveLine(id, baseLineCount() + ++added))
                    return false;
            } else if (c.scheduleChanged && !m_store.updateLine(c.line)) {
                return false;
            }
            Entry entry = m_data.entry(periodId, id);
            const Cents before = c.action == ImportChange::Update ? m_data.line(id)->scheduled : 0;
            if (c.billing)
                entry.completed = c.completed;
            else if (c.scheduleChanged && before != c.line.scheduled && (entry.completed || entry.stored))
                entry.completed = percentOf(c.line.scheduled, percentage(entry.completed, before));
            else
                continue;
            if (std::llabs(entry.completed + entry.stored) > std::llabs(c.line.scheduled))
                entry.stored = c.line.scheduled - entry.completed;
            if (periodId && !m_store.setEntry(periodId, id, entry))
                return false;
        }
        return true;
    });
    if (!ok)
        return false;
    cancelImport();
    QStringList parts;
    if (plan.updates)
        parts << tr("%n updated", "", plan.updates);
    if (plan.additions)
        parts << tr("%n added", "", plan.additions);
    if (plan.billed)
        parts << tr("%n billed", "", plan.billed);
    if (plan.skipped)
        parts << tr("%n skipped", "", plan.skipped);
    if (parts.isEmpty())
        parts << tr("nothing to change");
    setNotice(tr("Imported %1: %2.").arg(file, parts.join(", ")));
    return true;
}

// Export.

bool Backend::exportPayApplicationTo(const QString &chosen) {
    if (!current())
        return reject(tr("Start an application before exporting it."));
    QString path = chosen;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive))
        path += ".pdf";
    QString error;
    if (!writePayApplication(m_data, m_currentPeriod, path, &error))
        return reject(error);
    rememberFolder(path);
    m_lastExport = path;
    setNotice(tr("Saved application %1 to %2").arg(QLocale().toString(current()->number), QFileInfo(path).fileName()));
    return true;
}

void Backend::openExport() {
    if (!m_lastExport.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastExport));
}

// Selection, messages, and formatting.

void Backend::selectLine(int id) {
    if (id == m_selectedLine)
        return;
    m_selectedLine = id;
    emit selectionChanged();
}

void Backend::selectRelative(int step) {
    const int count = m_lines.count();
    if (!count)
        return;
    const int index = m_lines.indexOf(m_selectedLine);
    const int next = index < 0 ? (step > 0 ? 0 : count - 1) : std::clamp(index + step, 0, count - 1);
    selectLine(m_lines.idAt(next));
}

void Backend::setError(const QString &message) {
    m_error = message;
    emit errorChanged();
}

void Backend::clearError() {
    if (m_error.isEmpty())
        return;
    m_error.clear();
    emit errorChanged();
}

void Backend::setNotice(const QString &message) {
    m_notice = message;
    if (message.isEmpty())
        m_lastExport.clear();
    emit noticeChanged();
}

QString Backend::money(double cents) const { return formatMoney(Cents(std::llround(cents)), m_data.info.currency); }

QString Backend::percent(double value) const { return formatPercent(value); }

QString Backend::quantity(double value) const {
    // Up to three decimals, without trailing zeros.
    QString text = QLocale().toString(value, 'f', 3);
    const QChar point = QLocale().decimalPoint().at(0);
    if (text.contains(point)) {
        while (text.endsWith('0'))
            text.chop(1);
        if (text.endsWith(point))
            text.chop(1);
    }
    return text;
}

QString Backend::date(const QVariant &value) const {
    const QDate d = toDate(value);
    return d.isValid() ? QLocale().toString(d, QLocale::ShortFormat) : QString();
}
