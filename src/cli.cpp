#include "cli.h"

#include "backend.h"
#include "billing.h"
#include "filepicker.h"
#include "money.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <cstdio>

namespace {
const QStringList Commands = {"new",     "info",          "set-info", "lines",   "add-line", "set-line",
                              "remove-line", "import",    "periods",  "start",   "set-period", "bill",
                              "submit",  "reopen",        "change-orders", "change-order", "approve",
                              "summary", "export",        "skill",    "help"};

void print(FILE *stream, const QString &text) { fprintf(stream, "%s\n", qPrintable(text)); }
void print(const QJsonObject &object) { fprintf(stdout, "%s", QJsonDocument(object).toJson().constData()); }
int fail(const QString &message) {
    print(stderr, message);
    return 1;
}

// Commands never show a dialog.
class NoPicker : public FilePicker {
public:
    void open(const QString &, const QString &, const QList<FileFilter> &, bool, const QString &) override {}
    void save(const QString &, const QString &, const QString &, const FileFilter &) override {}
};

double amount(Cents cents) { return double(cents) / 100.0; }
QString isoDate(const QDate &date) { return date.isValid() ? date.toString(Qt::ISODate) : QString(); }

// How the sheet numbers a line: its code, CO and the change order's
// number, or its place among the original lines.
QString lineNumber(const ProjectData &data, const Line &line) {
    if (!line.code.isEmpty())
        return line.code;
    if (line.isChangeOrder())
        return "CO" + QString::number(line.coNumber);
    int n = 0;
    for (const auto &l : data.lines)
        if (!l.isChangeOrder()) {
            ++n;
            if (l.id == line.id)
                break;
        }
    return QString::number(n);
}

QJsonObject toJson(const ProjectData &data, const BillingRow &row) {
    return {{"item", lineNumber(data, row.line)},
            {"code", row.line.code},
            {"description", row.line.description},
            {"changeOrder", row.line.isChangeOrder() ? QJsonValue(row.line.coNumber) : QJsonValue()},
            {"quantity", row.line.quantity},
            {"unit", row.line.unit},
            {"unitPrice", amount(row.line.unitPrice)},
            {"scheduled", amount(row.scheduled)},
            {"previous", amount(row.previous)},
            {"thisPeriod", amount(row.thisPeriod)},
            {"stored", amount(row.stored)},
            {"toDate", amount(row.toDate)},
            {"percentComplete", std::round(row.percentComplete * 100) / 100},
            {"balance", amount(row.balance)},
            {"retainage", amount(row.retainage)}};
}

QJsonObject toJson(const Period &period) {
    return {{"number", period.number},
            {"periodTo", isoDate(period.periodTo)},
            {"applicationDate", isoDate(period.applicationDate)},
            {"retainageWork", period.retainageWork},
            {"retainageStored", period.retainageStored},
            {"status", period.submitted ? "submitted" : "draft"}};
}

QJsonObject toJson(const BillingSummary &s) {
    return {{"originalContract", amount(s.originalContract)},
            {"changeOrders", amount(s.changeOrders)},
            {"contractToDate", amount(s.contractToDate)},
            {"completedToDate", amount(s.completedToDate)},
            {"retainageWork", amount(s.retainageWork)},
            {"retainageStored", amount(s.retainageStored)},
            {"retainage", amount(s.retainage)},
            {"earnedLessRetainage", amount(s.earnedLessRetainage)},
            {"previousCertificates", amount(s.previousCertificates)},
            {"currentDue", amount(s.currentDue)},
            {"balanceToFinish", amount(s.balanceToFinish)},
            {"percentComplete", std::round(s.percentComplete * 100) / 100}};
}

QJsonObject toJson(const ProjectInfo &info) {
    return {{"name", info.name},
            {"location", info.location},
            {"owner", info.owner},
            {"contractor", info.contractor},
            {"architect", info.architect},
            {"contractFor", info.contractFor},
            {"contractNumber", info.contractNumber},
            {"contractDate", isoDate(info.contractDate)},
            {"currency", info.currency},
            {"retainageWork", info.retainageWork},
            {"retainageStored", info.retainageStored}};
}

struct Command {
    QCommandLineParser parser;
    NoPicker picker;
    Backend backend{&picker};

    Command(const QString &name, const QString &description, const QString &extra = {}) {
        parser.setApplicationDescription(description);
        parser.addHelpOption();
        parser.addPositionalArgument(name, description, name);
        parser.addPositionalArgument("project", "Spire project file", "<project.spire>");
        if (!extra.isEmpty())
            parser.addPositionalArgument(extra, extra, "<" + extra + ">");
        parser.addOption({"json", "Print the result as JSON"});
    }
    void option(const QString &name, const QString &description, const QString &value = {}) {
        parser.addOption(value.isEmpty() ? QCommandLineOption(name, description)
                                         : QCommandLineOption(name, description, value));
    }
    void periodOption() { option("period", "Application number (default: the latest)", "number"); }
    bool json() const { return parser.isSet("json"); }
    QString argument(int index) const { return parser.positionalArguments().value(index); }
    const ProjectData &data() const { return backend.data(); }

    bool open() {
        if (argument(1).isEmpty())
            return !fail("Name a Spire project.");
        if (!backend.openProjectAt(QFileInfo(argument(1)).absoluteFilePath(), false))
            return !fail(backend.error());
        return true;
    }
    // Puts the application named by --period on screen.
    bool choosePeriod(bool required = true) {
        if (parser.isSet("period")) {
            const Period *period = data().periodNumber(parser.value("period").toInt());
            if (!period)
                return !fail(QString("There is no application %1; spire periods lists them.").arg(parser.value("period")));
            backend.setCurrentPeriod(period->id);
        }
        if (required && !backend.currentPeriod())
            return !fail("There is no application yet; spire start begins the first one.");
        return true;
    }
    // A line named by its code, its item number, or CO and a change order
    // number.
    const Line *findLine(const QString &reference) const {
        const QString ref = reference.trimmed();
        for (const auto &line : data().lines)
            if (!line.code.isEmpty() && line.code.compare(ref, Qt::CaseInsensitive) == 0)
                return &line;
        for (const auto &line : data().lines)
            if (lineNumber(data(), line).compare(ref, Qt::CaseInsensitive) == 0 ||
                (line.isChangeOrder() && QString("CO %1").arg(line.coNumber).compare(ref, Qt::CaseInsensitive) == 0))
                return &line;
        fail(QString("There is no line %1; spire lines lists them.").arg(reference));
        return nullptr;
    }
    // Runs backend calls for each option given; false at the first refusal.
    bool apply(const QList<QPair<QString, std::function<bool(const QString &)>>> &setters) {
        for (const auto &[name, set] : setters)
            if (parser.isSet(name) && !set(parser.value(name)))
                return !fail(backend.error().isEmpty() ? QString("Could not set --%1.").arg(name) : backend.error());
        return true;
    }
    int done(const QString &text, const QJsonObject &result = {}) {
        if (json()) {
            QJsonObject object = result;
            object.insert("ok", true);
            print(object);
        } else {
            print(stdout, text);
        }
        return 0;
    }
};

void infoOptions(Command &command) {
    command.option("name", "Project name", "text");
    command.option("location", "Project address", "text");
    command.option("owner", "Owner the application goes to", "text");
    command.option("contractor", "Contractor applying for payment", "text");
    command.option("architect", "Architect certifying it", "text");
    command.option("contract-for", "What the contract is for", "text");
    command.option("contract-number", "Contract number", "text");
    command.option("contract-date", "Contract date, YYYY-MM-DD", "date");
    command.option("currency", "Currency symbol, e.g. $", "symbol");
    command.option("retainage", "Retainage on completed work, percent", "percent");
    command.option("retainage-stored", "Retainage on stored materials, percent", "percent");
}

bool applyInfo(Command &command) {
    QList<QPair<QString, std::function<bool(const QString &)>>> setters;
    const QList<QPair<QString, QString>> fields = {
        {"name", "name"},         {"location", "location"},   {"owner", "owner"},
        {"contractor", "contractor"}, {"architect", "architect"}, {"contract-for", "contractFor"},
        {"contract-number", "contractNumber"}, {"currency", "currency"}, {"retainage", "retainageWork"},
        {"retainage-stored", "retainageStored"}};
    for (const auto &[option, field] : fields)
        setters.append({option, [&command, field](const QString &v) { return command.backend.setInfo(field, v); }});
    setters.append({"contract-date", [&command](const QString &v) {
                        const QDate date = QDate::fromString(v, Qt::ISODate);
                        if (!v.isEmpty() && !date.isValid()) {
                            fail("--contract-date takes a date as YYYY-MM-DD.");
                            return false;
                        }
                        return command.backend.setInfo("contractDate", date);
                    }});
    return command.apply(setters);
}

void lineOptions(Command &command) {
    command.option("description", "Description of the work", "text");
    command.option("code", "Item or cost code", "code");
    command.option("scheduled", "Scheduled value", "amount");
    command.option("quantity", "Quantity (with --unit-price, sets the scheduled value)", "number");
    command.option("unit", "Unit of the quantity, e.g. m2", "unit");
    command.option("unit-price", "Price per unit", "amount");
}

bool applyLine(Command &command, int id) {
    QList<QPair<QString, std::function<bool(const QString &)>>> setters;
    for (const auto &[option, field] : QList<QPair<QString, QString>>{{"description", "description"},
                                                                    {"code", "code"},
                                                                    {"quantity", "quantity"},
                                                                    {"unit", "unit"},
                                                                    {"unit-price", "unitPrice"},
                                                                    {"scheduled", "scheduled"}})
        setters.append({option, [&command, id, field](const QString &v) { return command.backend.setLineField(id, field, v); }});
    return command.apply(setters);
}

int newProject(const QStringList &arguments) {
    Command command("new", "Create a project.");
    infoOptions(command);
    command.parser.process(arguments);
    QString path = command.argument(1);
    if (path.isEmpty())
        return fail("Name the project file to create.");
    if (!path.endsWith(".spire"))
        path += ".spire";
    if (!command.backend.createProjectAt(QFileInfo(path).absoluteFilePath(), false))
        return fail(command.backend.error());
    if (!applyInfo(command))
        return 1;
    return command.done("Created " + QFileInfo(path).absoluteFilePath(),
                        {{"project", QFileInfo(path).absoluteFilePath()}, {"info", toJson(command.data().info)}});
}

int info(const QStringList &arguments) {
    Command command("info", "Show the project's details.");
    command.parser.process(arguments);
    if (!command.open())
        return 1;
    const ProjectInfo &i = command.data().info;
    if (command.json())
        return command.done({}, {{"info", toJson(i)}});
    print(stdout, i.name);
    for (const auto &[label, value] : QList<QPair<QString, QString>>{{"Location", i.location},
                                                                   {"Owner", i.owner},
                                                                   {"Contractor", i.contractor},
                                                                   {"Architect", i.architect},
                                                                   {"Contract for", i.contractFor},
                                                                   {"Contract no.", i.contractNumber},
                                                                   {"Contract date", isoDate(i.contractDate)}})
        if (!value.isEmpty())
            print(stdout, QString("%1: %2").arg(label, value));
    print(stdout, QString("Retainage: %1% work, %2% stored").arg(i.retainageWork).arg(i.retainageStored));
    return 0;
}

int setInfo(const QStringList &arguments) {
    Command command("set-info", "Change the project's details.");
    infoOptions(command);
    command.parser.process(arguments);
    if (!command.open() || !applyInfo(command))
        return 1;
    return command.done("Updated " + command.data().info.name, {{"info", toJson(command.data().info)}});
}

int lines(const QStringList &arguments) {
    Command command("lines", "List the continuation sheet (G703) for an application.");
    command.periodOption();
    command.parser.process(arguments);
    if (!command.open() || !command.choosePeriod(false))
        return 1;
    Period first;
    first.number = 1;
    first.retainageWork = command.data().info.retainageWork;
    first.retainageStored = command.data().info.retainageStored;
    const Period *current = command.data().period(command.backend.currentPeriod());
    const Billing billing = computeBilling(command.data(), current ? *current : first);
    if (command.json()) {
        QJsonArray rows;
        for (const auto &row : billing.rows)
            rows.append(toJson(command.data(), row));
        return command.done({}, {{"period", current ? QJsonValue(current->number) : QJsonValue()}, {"lines", rows}});
    }
    const QString currency = command.data().info.currency;
    for (const auto &row : billing.rows)
        print(stdout, QString("%1\t%2\t%3 scheduled\t%4% complete\t%5 this period")
                          .arg(lineNumber(command.data(), row.line), row.line.description,
                               formatMoney(row.scheduled, currency, QLocale()))
                          .arg(row.percentComplete, 0, 'f', 1)
                          .arg(formatMoney(row.thisPeriod, currency, QLocale())));
    return 0;
}

int addLine(const QStringList &arguments) {
    Command command("add-line", "Add a line to the schedule of values.");
    lineOptions(command);
    command.parser.process(arguments);
    if (!command.open())
        return 1;
    if (command.parser.value("description").trimmed().isEmpty())
        return fail("Give the line a --description.");
    const int id = command.backend.addLine();
    if (!id)
        return fail(command.backend.error());
    if (!applyLine(command, id))
        return 1;
    const Line *line = command.data().line(id);
    return command.done("Added line " + lineNumber(command.data(), *line),
                        {{"item", lineNumber(command.data(), *line)}, {"scheduled", amount(line->scheduled)}});
}

int setLine(const QStringList &arguments) {
    Command command("set-line", "Change a line of the schedule of values.", "item");
    lineOptions(command);
    command.parser.process(arguments);
    if (!command.open())
        return 1;
    const Line *line = command.findLine(command.argument(2));
    if (!line)
        return 1;
    const int id = line->id;
    if (!applyLine(command, id))
        return 1;
    line = command.data().line(id);
    return command.done("Updated line " + lineNumber(command.data(), *line),
                        {{"item", lineNumber(command.data(), *line)}, {"scheduled", amount(line->scheduled)}});
}

int removeLine(const QStringList &arguments) {
    Command command("remove-line", "Delete a line (or a change order) that has not been billed.", "item");
    command.parser.process(arguments);
    if (!command.open())
        return 1;
    const Line *line = command.findLine(command.argument(2));
    if (!line)
        return 1;
    const QString number = lineNumber(command.data(), *line);
    if (!command.backend.removeLine(line->id))
        return fail(command.backend.error());
    return command.done("Deleted line " + number, {{"item", number}});
}

int importTakeoff(const QStringList &arguments) {
    Command command("import", "Import quantities from a Plumb CSV export.", "takeoff.csv");
    command.periodOption();
    command.option("dry-run", "Show what would change without changing anything");
    command.parser.process(arguments);
    if (!command.open() || !command.choosePeriod(false))
        return 1;
    if (command.argument(2).isEmpty())
        return fail("Name the Plumb CSV to import.");
    if (!command.backend.importTakeoffFrom(command.argument(2)))
        return fail(command.backend.error());
    const QVariantMap preview = command.backend.importPreview();
    QJsonArray rows;
    QStringList text;
    static const char *Actions[] = {"update", "add", "skip", "unchanged"};
    for (const auto &value : preview.value("rows").toList()) {
        const QVariantMap row = value.toMap();
        QJsonObject object{{"action", Actions[row.value("action").toInt()]},
                           {"code", row.value("code").toString()},
                           {"description", row.value("description").toString()},
                           {"quantity", row.value("quantity").toDouble()},
                           {"unit", row.value("unit").toString()},
                           {"scheduled", row.value("scheduled").toDouble() / 100}};
        if (row.value("billing").toBool())
            object.insert("percentComplete", std::round(row.value("percent").toDouble() * 100) / 100);
        if (!row.value("note").toString().isEmpty())
            object.insert("note", row.value("note").toString());
        rows.append(object);
        text << QString("%1\t%2\t%3").arg(Actions[row.value("action").toInt()], row.value("description").toString(),
                                          row.value("note").toString());
    }
    const QJsonObject result{{"rows", rows},
                             {"updated", preview.value("updates").toInt()},
                             {"added", preview.value("additions").toInt()},
                             {"billed", preview.value("billed").toInt()},
                             {"skipped", preview.value("skipped").toInt()},
                             {"unchanged", preview.value("unchanged").toInt()},
                             {"warnings", QJsonArray::fromStringList(preview.value("warnings").toStringList())}};
    for (const auto &warning : preview.value("warnings").toStringList())
        print(stderr, warning);
    if (command.parser.isSet("dry-run")) {
        command.backend.cancelImport();
        return command.done(text.join("\n"), result);
    }
    if (!command.backend.applyImport())
        return fail(command.backend.error());
    return command.done(command.backend.notice(), result);
}

int periods(const QStringList &arguments) {
    Command command("periods", "List the applications for payment.");
    command.parser.process(arguments);
    if (!command.open())
        return 1;
    QJsonArray list;
    for (const auto &period : command.data().periods) {
        QJsonObject object = toJson(period);
        object.insert("currentDue", amount(computeBilling(command.data(), period.id).summary.currentDue));
        list.append(object);
        if (!command.json())
            print(stdout, QString("%1\tto %2\t%3\t%4 due")
                              .arg(period.number)
                              .arg(isoDate(period.periodTo), period.submitted ? "submitted" : "draft",
                                   formatMoney(computeBilling(command.data(), period.id).summary.currentDue,
                                               command.data().info.currency, QLocale())));
    }
    if (command.json())
        return command.done({}, {{"periods", list}});
    return 0;
}

void periodFieldOptions(Command &command) {
    command.option("period-to", "End of the period, YYYY-MM-DD", "date");
    command.option("application-date", "Date of the application, YYYY-MM-DD", "date");
    command.option("retainage", "Retainage on completed work, percent", "percent");
    command.option("retainage-stored", "Retainage on stored materials, percent", "percent");
}

bool applyPeriodFields(Command &command) {
    auto date = [&command](const char *field) {
        return [&command, field](const QString &v) {
            const QDate d = QDate::fromString(v, Qt::ISODate);
            if (!d.isValid()) {
                fail("Dates are written YYYY-MM-DD.");
                return false;
            }
            return command.backend.setPeriodField(field, d);
        };
    };
    return command.apply({{"period-to", date("periodTo")},
                          {"application-date", date("applicationDate")},
                          {"retainage", [&command](const QString &v) { return command.backend.setPeriodField("retainageWork", v); }},
                          {"retainage-stored", [&command](const QString &v) { return command.backend.setPeriodField("retainageStored", v); }}});
}

int start(const QStringList &arguments) {
    Command command("start", "Start the next application, carrying work to date over.");
    periodFieldOptions(command);
    command.parser.process(arguments);
    if (!command.open())
        return 1;
    if (!command.backend.startPeriod())
        return fail(command.backend.error());
    if (!applyPeriodFields(command))
        return 1;
    const Period *period = command.data().period(command.backend.currentPeriod());
    return command.done(QString("Started application %1").arg(period->number), {{"period", toJson(*period)}});
}

int setPeriod(const QStringList &arguments) {
    Command command("set-period", "Change an application's dates or retainage.");
    command.periodOption();
    periodFieldOptions(command);
    command.parser.process(arguments);
    if (!command.open() || !command.choosePeriod() || !applyPeriodFields(command))
        return 1;
    const Period *period = command.data().period(command.backend.currentPeriod());
    return command.done(QString("Updated application %1").arg(period->number), {{"period", toJson(*period)}});
}

int bill(const QStringList &arguments) {
    Command command("bill", "Bill a line on an application.", "item");
    command.periodOption();
    command.option("percent", "Work complete to date, percent of the scheduled value", "percent");
    command.option("this-period", "Work completed this period, as an amount", "amount");
    command.option("installed", "Quantity installed to date", "number");
    command.option("stored", "Materials presently stored, as an amount", "amount");
    command.option("complete", "Bill the line in full");
    command.parser.process(arguments);
    if (!command.open() || !command.choosePeriod())
        return 1;
    const Line *line = command.findLine(command.argument(2));
    if (!line)
        return 1;
    const int id = line->id;
    Backend &b = command.backend;
    if (command.parser.isSet("complete") && !b.completeLine(id))
        return fail(b.error());
    if (!command.apply({{"percent", [&](const QString &v) { return b.setPercent(id, v); }},
                        {"this-period", [&](const QString &v) { return b.setThisPeriod(id, v); }},
                        {"installed", [&](const QString &v) { return b.setInstalled(id, v); }},
                        {"stored", [&](const QString &v) { return b.setStored(id, v); }}}))
        return 1;
    const Billing billing = computeBilling(command.data(), b.currentPeriod());
    for (const auto &row : billing.rows)
        if (row.line.id == id)
            return command.done(QString("Line %1: %2% complete, %3 this period")
                                    .arg(lineNumber(command.data(), row.line))
                                    .arg(row.percentComplete, 0, 'f', 1)
                                    .arg(formatMoney(row.thisPeriod, command.data().info.currency, QLocale())),
                                {{"line", toJson(command.data(), row)}, {"summary", toJson(billing.summary)}});
    return fail("That line is not part of this application.");
}

int submit(const QStringList &arguments, bool reopen) {
    Command command(reopen ? "reopen" : "submit",
                    reopen ? "Reopen the latest application to change it." : "Mark an application submitted, fixing its amounts.");
    command.periodOption();
    command.parser.process(arguments);
    if (!command.open() || !command.choosePeriod())
        return 1;
    const bool ok = reopen ? command.backend.reopenPeriod() : command.backend.submitPeriod();
    if (!ok)
        return fail(command.backend.error().isEmpty() ? (reopen ? "That application is not submitted." : "That application is already submitted.")
                                                      : command.backend.error());
    const Period *period = command.data().period(command.backend.currentPeriod());
    return command.done(QString("Application %1 is %2").arg(period->number).arg(reopen ? "a draft again" : "submitted"),
                        {{"period", toJson(*period)}});
}

int changeOrders(const QStringList &arguments) {
    Command command("change-orders", "List change orders.");
    command.parser.process(arguments);
    if (!command.open())
        return 1;
    QJsonArray list;
    for (const auto &line : command.data().lines) {
        if (!line.isChangeOrder())
            continue;
        list.append(QJsonObject{{"number", line.coNumber},
                                {"description", line.description},
                                {"amount", amount(line.scheduled)},
                                {"date", isoDate(line.coDate)},
                                {"status", line.approved ? "approved" : "pending"},
                                {"approvedIn", line.approved ? QJsonValue(line.approvedIn) : QJsonValue()}});
        if (!command.json())
            print(stdout, QString("CO %1\t%2\t%3\t%4")
                              .arg(line.coNumber)
                              .arg(line.description, formatMoney(line.scheduled, command.data().info.currency, QLocale()),
                                   line.approved ? QString("approved in application %1").arg(line.approvedIn) : "pending"));
    }
    if (command.json())
        return command.done({}, {{"changeOrders", list}});
    return 0;
}

int changeOrder(const QStringList &arguments) {
    Command command("change-order", "Add a change order.");
    command.option("description", "What changed", "text");
    command.option("amount", "Amount; negative for a deduction", "amount");
    command.option("date", "Date of the change order, YYYY-MM-DD", "date");
    command.option("approve", "Approve it on the current application");
    command.parser.process(arguments);
    if (!command.open())
        return 1;
    if (command.parser.value("description").trimmed().isEmpty() || !command.parser.isSet("amount"))
        return fail("Give the change order a --description and an --amount.");
    Backend &b = command.backend;
    const int id = b.addChangeOrder();
    if (!id)
        return fail(b.error());
    if (!command.apply({{"description", [&](const QString &v) { return b.setChangeOrderField(id, "description", v); }},
                        {"amount", [&](const QString &v) { return b.setChangeOrderField(id, "amount", v); }},
                        {"date", [&](const QString &v) {
                             const QDate d = QDate::fromString(v, Qt::ISODate);
                             return d.isValid() ? b.setChangeOrderField(id, "date", d) : !fail("Dates are written YYYY-MM-DD.");
                         }}}))
        return 1;
    if (command.parser.isSet("approve") && !b.setApproved(id, true))
        return fail(b.error());
    const Line *line = command.data().line(id);
    return command.done(QString("Added change order %1").arg(line->coNumber),
                        {{"number", line->coNumber}, {"approved", line->approved}});
}

int approve(const QStringList &arguments) {
    Command command("approve", "Approve a change order, or with --undo set it back to pending.", "number");
    command.option("undo", "Set it back to pending");
    command.parser.process(arguments);
    if (!command.open())
        return 1;
    const QString wanted = command.argument(2).toUpper().remove("CO").trimmed();
    for (const auto &line : command.data().lines)
        if (line.isChangeOrder() && QString::number(line.coNumber) == wanted) {
            const int id = line.id;
            const bool approved = !command.parser.isSet("undo");
            if (line.approved != approved && !command.backend.setApproved(id, approved))
                return fail(command.backend.error());
            const Line *now = command.data().line(id);
            return command.done(approved ? QString("Change order %1 approved in application %2").arg(now->coNumber).arg(now->approvedIn)
                                         : QString("Change order %1 is pending").arg(now->coNumber),
                                {{"number", now->coNumber}, {"approved", now->approved}, {"approvedIn", now->approvedIn}});
        }
    return fail(QString("There is no change order %1.").arg(command.argument(2)));
}

int summary(const QStringList &arguments) {
    Command command("summary", "Show the application for payment (G702) totals.");
    command.periodOption();
    command.parser.process(arguments);
    if (!command.open() || !command.choosePeriod())
        return 1;
    const Billing billing = computeBilling(command.data(), command.backend.currentPeriod());
    if (command.json())
        return command.done({}, {{"period", toJson(billing.period)}, {"summary", toJson(billing.summary)}});
    const BillingSummary &s = billing.summary;
    const QString c = command.data().info.currency;
    auto m = [&](Cents v) { return formatMoney(v, c, QLocale()); };
    print(stdout, QString("Application %1, period to %2 (%3)")
                      .arg(billing.period.number)
                      .arg(isoDate(billing.period.periodTo), billing.period.submitted ? "submitted" : "draft"));
    for (const auto &[label, value] : QList<QPair<QString, Cents>>{{"1. Original contract sum", s.originalContract},
                                                                 {"2. Net change by change orders", s.changeOrders},
                                                                 {"3. Contract sum to date", s.contractToDate},
                                                                 {"4. Total completed and stored to date", s.completedToDate},
                                                                 {"5. Retainage", s.retainage},
                                                                 {"6. Total earned less retainage", s.earnedLessRetainage},
                                                                 {"7. Less previous certificates", s.previousCertificates},
                                                                 {"8. Current payment due", s.currentDue},
                                                                 {"9. Balance to finish, including retainage", s.balanceToFinish}})
        print(stdout, QString("%1\t%2").arg(label, m(value)));
    return 0;
}

int exportPayApp(const QStringList &arguments) {
    Command command("export", "Write the pay application (G702 and G703) as a PDF.", "application.pdf");
    command.periodOption();
    command.parser.process(arguments);
    if (!command.open() || !command.choosePeriod())
        return 1;
    const QString target = command.argument(2);
    if (target.isEmpty())
        return fail("Name the PDF to write.");
    if (!command.backend.exportPayApplicationTo(QFileInfo(target).absoluteFilePath()))
        return fail(command.backend.error());
    const QString written = command.backend.lastExport();
    return command.done("Wrote " + written, {{"file", written}});
}

int skill(const QStringList &arguments) {
    if (arguments.value(2) != "install")
        return fail("Usage: spire skill install");
    const QString directory = QDir::homePath() + "/.claude/skills/spire";
    QFile source(":/skill.md");
    if (!QDir().mkpath(directory) || !source.open(QIODevice::ReadOnly))
        return fail("Could not install the skill to " + directory);
    QSaveFile target(directory + "/SKILL.md");
    if (!target.open(QIODevice::WriteOnly) || target.write(source.readAll()) < 0 || !target.commit())
        return fail("Could not write " + target.fileName());
    print(stdout, "Installed " + target.fileName());
    return 0;
}
}

bool isCliCommand(const char *argument) { return Commands.contains(QString::fromUtf8(argument)); }

QString cliSummary() {
    return "Commands:\n"
           "  open [project]                      Open the app\n"
           "  new <project> [details]             Create a project\n"
           "  info | set-info <project>           Show or change the project's details\n"
           "  lines <project> [--period N]        The continuation sheet (G703)\n"
           "  add-line <project> --description .. Add a line to the schedule of values\n"
           "  set-line | remove-line <project> <item>\n"
           "  import <project> <takeoff.csv>      Import quantities from Plumb (--dry-run to look first)\n"
           "  periods <project>                   List the applications\n"
           "  start <project>                     Start the next application\n"
           "  set-period <project> [--period N]   Dates and retainage of an application\n"
           "  bill <project> <item> --percent ..  Bill a line (or --this-period, --installed, --stored, --complete)\n"
           "  submit | reopen <project>           Fix an application's amounts, or reopen the latest\n"
           "  change-orders <project>             List change orders\n"
           "  change-order <project> --description .. --amount ..  Add one (--approve to approve it)\n"
           "  approve <project> <number> [--undo] Approve a change order\n"
           "  summary <project> [--period N]      The application for payment (G702) totals\n"
           "  export <project> <application.pdf>  Write the pay application PDF\n"
           "  skill install                       Install the agent skill\n\n"
           "Every command takes --help, and --json for machine-readable output.";
}

int runCli(const QStringList &arguments) {
    const QString name = arguments.value(1);
    if (name.isEmpty() || name == "help") {
        print(stdout, "Spire — progress billing for contractors.\n\n" + cliSummary());
        return 0;
    }
    if (name == "new") return newProject(arguments);
    if (name == "info") return info(arguments);
    if (name == "set-info") return setInfo(arguments);
    if (name == "lines") return lines(arguments);
    if (name == "add-line") return addLine(arguments);
    if (name == "set-line") return setLine(arguments);
    if (name == "remove-line") return removeLine(arguments);
    if (name == "import") return importTakeoff(arguments);
    if (name == "periods") return periods(arguments);
    if (name == "start") return start(arguments);
    if (name == "set-period") return setPeriod(arguments);
    if (name == "bill") return bill(arguments);
    if (name == "submit") return submit(arguments, false);
    if (name == "reopen") return submit(arguments, true);
    if (name == "change-orders") return changeOrders(arguments);
    if (name == "change-order") return changeOrder(arguments);
    if (name == "approve") return approve(arguments);
    if (name == "summary") return summary(arguments);
    if (name == "export") return exportPayApp(arguments);
    if (name == "skill") return skill(arguments);
    return fail("Unknown command " + name);
}
