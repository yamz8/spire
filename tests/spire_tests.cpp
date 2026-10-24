#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "billing.h"
#include "cli.h"
#include "fakepicker.h"
#include "money.h"
#include "payapp.h"
#include "plumbimport.h"
#include "store.h"
#include "theme.h"
#include "translator.h"

// A project on disk, open in a backend, with a three-line schedule of
// values worth $100,000 and 10% retainage.
struct Project {
    QTemporaryDir dir;
    FakePicker picker;
    Backend backend{&picker};
    int footings = 0, steel = 0, finishes = 0;

    Project() {
        backend.createProjectAt(path(), false);
        footings = addLine("Footings", "20000");
        steel = addLine("Structural steel", "50000");
        finishes = addLine("Finishes", "30000");
    }
    QString path() const { return dir.filePath("job.spire"); }
    int addLine(const QString &description, const QString &scheduled) {
        const int id = backend.addLine();
        backend.setLineField(id, "description", description);
        backend.setLineField(id, "scheduled", scheduled);
        return id;
    }
    Billing billing() const { return computeBilling(backend.data(), backend.currentPeriod()); }
    BillingRow row(int lineId) const {
        for (const auto &row : billing().rows)
            if (row.line.id == lineId)
                return row;
        return {};
    }
    Entry entry(int lineId) const { return backend.data().entry(backend.currentPeriod(), lineId); }
    QString write(const QString &name, const QByteArray &contents) const {
        QFile file(dir.filePath(name));
        file.open(QIODevice::WriteOnly);
        file.write(contents);
        return file.fileName();
    }
};

static void write(const QString &path, const QByteArray &contents) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

static Cents dollars(double amount) { return std::llround(amount * 100); }

class SpireTests : public QObject {
    Q_OBJECT

private slots:
    void init() { QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates)); }

    void storeCreatesAndReopens()
    {
        QTemporaryDir dir;
        ProjectInfo info;
        info.name = "Harbor Street";
        info.owner = "Harbor Holdings";
        info.retainageWork = 5;
        {
            Store store;
            QVERIFY(store.create(dir.filePath("a.spire"), info));
            Line line;
            line.description = "Footings";
            line.scheduled = 123456;
            QVERIFY(store.insertLine(line));
        }
        Store store;
        QVERIFY(store.open(dir.filePath("a.spire")));
        const ProjectData data = store.load();
        QCOMPARE(data.info.name, QString("Harbor Street"));
        QCOMPARE(data.info.owner, QString("Harbor Holdings"));
        QCOMPARE(data.info.retainageWork, 5.0);
        QCOMPARE(data.lines.size(), 1);
        QCOMPARE(data.lines[0].scheduled, Cents(123456));
        QVERIFY(!store.create(dir.filePath("a.spire"), info));
    }

    void storeRefusesWhatIsNotAProject()
    {
        QTemporaryDir dir;
        write(dir.filePath("notes.spire"), "just some text");
        Store store;
        QVERIFY(!store.open(dir.filePath("notes.spire")));
        QVERIFY(store.error().contains("not a Spire project"));
        QVERIFY(!store.open(dir.filePath("missing.spire")));
    }

    void snapshotsRestoreTheWholeProject()
    {
        Project p;
        p.backend.startPeriod();
        p.backend.setPercent(p.steel, "40");
        Store store;
        QVERIFY(store.open(p.path()));
        const QJsonObject before = store.snapshot();
        QVERIFY(store.removeLine(p.steel));
        QVERIFY(store.restore(before));
        QCOMPARE(store.snapshot(), before);
        QCOMPARE(store.load().entry(p.backend.currentPeriod(), p.steel).completed, dollars(20000));
    }

    void percentCompleteBillsTheLine()
    {
        Project p;
        QVERIFY(p.backend.startPeriod());
        QVERIFY(p.backend.setPercent(p.footings, "100"));
        QVERIFY(p.backend.setPercent(p.steel, "40"));
        const BillingSummary s = p.billing().summary;
        QCOMPARE(s.originalContract, dollars(100000));
        QCOMPARE(s.completedToDate, dollars(40000));
        QCOMPARE(s.retainage, dollars(4000));
        QCOMPARE(s.earnedLessRetainage, dollars(36000));
        QCOMPARE(s.currentDue, dollars(36000));
        QCOMPARE(s.balanceToFinish, dollars(64000));
        QCOMPARE(p.row(p.steel).percentComplete, 40.0);
    }

    void amountsAndStoredMaterialsBillToo()
    {
        Project p;
        p.backend.startPeriod();
        QVERIFY(p.backend.setThisPeriod(p.steel, "12,500.50"));
        QVERIFY(p.backend.setStored(p.steel, "8000"));
        QCOMPARE(p.row(p.steel).thisPeriod, dollars(12500.50));
        QCOMPARE(p.row(p.steel).stored, dollars(8000));
        QCOMPARE(p.row(p.steel).toDate, dollars(20500.50));
        // Nothing bills past the scheduled value.
        QVERIFY(!p.backend.setStored(p.steel, "40000"));
        QVERIFY(p.backend.error().contains("scheduled value"));
        QVERIFY(!p.backend.setPercent(p.steel, "120"));
        QVERIFY(!p.backend.setPercent(p.steel, "lots"));
        QCOMPARE(p.row(p.steel).stored, dollars(8000));
        // Billing it in full moves stored materials into the work.
        QVERIFY(p.backend.completeLine(p.steel));
        QCOMPARE(p.row(p.steel).completed, dollars(50000));
        QCOMPARE(p.row(p.steel).stored, 0);
    }

    void retainageFollowsEachPeriodsRates()
    {
        Project p;
        p.backend.startPeriod();
        p.backend.setPercent(p.footings, "100");
        p.backend.setStored(p.steel, "10000");
        QVERIFY(p.backend.setPeriodField("retainageStored", "0"));
        QCOMPARE(p.billing().summary.retainageWork, dollars(2000));
        QCOMPARE(p.billing().summary.retainageStored, 0);
        QVERIFY(p.backend.submitPeriod());
        QCOMPARE(p.billing().summary.currentDue, dollars(28000));

        // Halving retainage on the next application releases half of what
        // was held, so it is paid now.
        QVERIFY(p.backend.startPeriod());
        QCOMPARE(p.backend.data().period(p.backend.currentPeriod())->retainageStored, 0.0);
        QVERIFY(p.backend.setPeriodField("retainageWork", "5"));
        QCOMPARE(p.billing().summary.previousCertificates, dollars(28000));
        QCOMPARE(p.billing().summary.currentDue, dollars(1000));
        QVERIFY(!p.backend.setPeriodField("retainageWork", "150"));
    }

    void startingAnApplicationCarriesWorkOver()
    {
        Project p;
        QVERIFY(p.backend.canStartPeriod());
        p.backend.startPeriod();
        QVERIFY(!p.backend.canStartPeriod());
        QVERIFY(!p.backend.startPeriod());
        p.backend.setPercent(p.steel, "40");
        p.backend.setStored(p.steel, "5000");
        const QDate firstEnd = p.backend.data().periods.first().periodTo;
        p.backend.submitPeriod();
        QVERIFY(p.backend.startPeriod());
        QCOMPARE(p.backend.data().periods.size(), 2);
        QCOMPARE(p.backend.data().periods.last().periodTo, QDate(firstEnd.addMonths(1).year(), firstEnd.addMonths(1).month(), firstEnd.addMonths(1).daysInMonth()));
        QCOMPARE(p.row(p.steel).previous, dollars(20000));
        QCOMPARE(p.row(p.steel).thisPeriod, 0);
        QCOMPARE(p.row(p.steel).stored, dollars(5000));
        p.backend.setPercent(p.steel, "70");
        QCOMPARE(p.row(p.steel).thisPeriod, dollars(15000));
    }

    void submittedApplicationsAreFixed()
    {
        Project p;
        p.backend.startPeriod();
        p.backend.setPercent(p.steel, "40");
        QVERIFY(p.backend.submitPeriod());
        QVERIFY(!p.backend.editable());
        QVERIFY(!p.backend.setPercent(p.steel, "50"));
        QVERIFY(p.backend.sovLocked());
        QVERIFY(!p.backend.addLine());
        QVERIFY(!p.backend.setLineField(p.steel, "scheduled", "60000"));
        QVERIFY(!p.backend.removeLine(p.footings));
        // Words can still be corrected.
        QVERIFY(p.backend.setLineField(p.steel, "description", "Structural steel, erected"));
        // Only the latest application reopens.
        QVERIFY(p.backend.reopenPeriod());
        QVERIFY(p.backend.setPercent(p.steel, "50"));
        p.backend.submitPeriod();
        p.backend.startPeriod();
        p.backend.stepPeriod(-1);
        QVERIFY(!p.backend.reopenPeriod());
        QVERIFY(!p.backend.removePeriod());
    }

    void aNewScheduledValueKeepsThePercent()
    {
        Project p;
        p.backend.startPeriod();
        p.backend.setPercent(p.steel, "40");
        QVERIFY(p.backend.setLineField(p.steel, "scheduled", "60000"));
        QCOMPARE(p.row(p.steel).completed, dollars(24000));
    }

    void quantitiesPriceTheLine()
    {
        Project p;
        QVERIFY(p.backend.setLineField(p.finishes, "quantity", "1,200"));
        QVERIFY(p.backend.setLineField(p.finishes, "unit", "m2"));
        QVERIFY(p.backend.setLineField(p.finishes, "unitPrice", "25.50"));
        QCOMPARE(p.backend.data().line(p.finishes)->scheduled, dollars(30600));
        p.backend.startPeriod();
        QVERIFY(p.backend.setInstalled(p.finishes, "300"));
        QCOMPARE(p.row(p.finishes).percentComplete, 25.0);
        QVERIFY(!p.backend.setInstalled(p.finishes, "1300"));
        QVERIFY(!p.backend.setInstalled(p.footings, "3"));
    }

    void changeOrdersBillFromTheirApproval()
    {
        Project p;
        p.backend.startPeriod();
        const int co = p.backend.addChangeOrder();
        QVERIFY(p.backend.setChangeOrderField(co, "description", "Canopy"));
        QVERIFY(p.backend.setChangeOrderField(co, "amount", "15000"));
        QCOMPARE(p.backend.pendingChangeOrders(), 1);
        // Pending, it is not on the sheet and cannot be billed.
        QCOMPARE(p.billing().rows.size(), 3);
        QVERIFY(!p.backend.setPercent(co, "10"));

        QVERIFY(p.backend.setApproved(co, true));
        QCOMPARE(p.backend.data().line(co)->approvedIn, 1);
        QCOMPARE(p.billing().rows.size(), 4);
        QCOMPARE(p.billing().summary.changeOrders, dollars(15000));
        QCOMPARE(p.billing().summary.contractToDate, dollars(115000));
        QCOMPARE(p.billing().summary.coThisAdditions, dollars(15000));
        QVERIFY(p.backend.setPercent(co, "20"));

        // Setting it back to pending takes its billing off the draft.
        QVERIFY(p.backend.setApproved(co, false));
        QCOMPARE(p.backend.data().entry(p.backend.currentPeriod(), co).completed, 0);
        p.backend.setApproved(co, true);
        p.backend.setPercent(co, "20");
        p.backend.submitPeriod();
        QVERIFY(!p.backend.setApproved(co, false));
        QVERIFY(!p.backend.setChangeOrderField(co, "amount", "1"));

        // A deduction approved on the next application.
        p.backend.startPeriod();
        const int deduct = p.backend.addChangeOrder();
        p.backend.setChangeOrderField(deduct, "amount", "-2500");
        p.backend.setApproved(deduct, true);
        const BillingSummary s = p.billing().summary;
        QCOMPARE(s.coPreviousAdditions, dollars(15000));
        QCOMPARE(s.coThisDeductions, dollars(-2500));
        QCOMPARE(s.changeOrders, dollars(12500));
        QCOMPARE(p.backend.data().line(deduct)->coNumber, 2);
    }

    void deletingADraftPutsItsChangeOrdersBack()
    {
        Project p;
        p.backend.startPeriod();
        p.backend.submitPeriod();
        p.backend.startPeriod();
        const int co = p.backend.addChangeOrder();
        p.backend.setChangeOrderField(co, "amount", "900");
        p.backend.setApproved(co, true);
        QCOMPARE(p.backend.data().line(co)->approvedIn, 2);
        QVERIFY(p.backend.removePeriod());
        QCOMPARE(p.backend.data().periods.size(), 1);
        QVERIFY(!p.backend.data().line(co)->approved);
        p.backend.undo();
        QCOMPARE(p.backend.data().periods.size(), 2);
        QVERIFY(p.backend.data().line(co)->approved);
    }

    void everyChangeUndoesAndRedoes()
    {
        Project p;
        p.backend.startPeriod();
        p.backend.setPercent(p.steel, "40");
        p.backend.setPercent(p.steel, "60");
        p.backend.undo();
        QCOMPARE(p.row(p.steel).percentComplete, 40.0);
        p.backend.undo();
        QCOMPARE(p.row(p.steel).percentComplete, 0.0);
        p.backend.redo();
        QCOMPARE(p.row(p.steel).percentComplete, 40.0);
        // A value set to what it already is leaves no step behind.
        const bool before = p.backend.canRedo();
        p.backend.setPercent(p.steel, "40");
        QCOMPARE(p.backend.canRedo(), before);
        p.backend.removeLine(p.finishes);
        QVERIFY(!p.backend.data().line(p.finishes));
        p.backend.undo();
        QVERIFY(p.backend.data().line(p.finishes));
    }

    void linesMoveWithinTheirKind()
    {
        Project p;
        const int co = p.backend.addChangeOrder();
        QVERIFY(p.backend.moveLine(p.finishes, -1));
        QCOMPARE(p.backend.data().lines[1].id, p.finishes);
        // Change orders stay after the original lines.
        QVERIFY(!p.backend.moveLine(co, -1));
        const int added = p.backend.addLine();
        QCOMPARE(p.backend.data().lines[3].id, added);
        QCOMPARE(p.backend.data().lines[4].id, co);
    }

    void openingABadFileKeepsTheProject()
    {
        Project p;
        write(p.dir.filePath("bad.spire"), "nope");
        QVERIFY(!p.backend.openProjectAt(p.dir.filePath("bad.spire"), false));
        QVERIFY(p.backend.isOpen());
        QCOMPARE(p.backend.data().lines.size(), 3);
    }

    void numbersAreReadAsWritten()
    {
        double v = 0;
        QVERIFY(parseNumber("1,234.50", &v)); QCOMPARE(v, 1234.5);
        QVERIFY(parseNumber("1.234,50", &v)); QCOMPARE(v, 1234.5);
        QVERIFY(parseNumber("$ 1 250", &v)); QCOMPARE(v, 1250.0);
        QVERIFY(parseNumber("12,5", &v)); QCOMPARE(v, 12.5);
        QVERIFY(parseNumber("1,250", &v)); QCOMPARE(v, 1250.0);
        QVERIFY(parseNumber("(300.00)", &v)); QCOMPARE(v, -300.0);
        QVERIFY(parseNumber("-42", &v)); QCOMPARE(v, -42.0);
        QVERIFY(parseNumber("45%", &v)); QCOMPARE(v, 45.0);
        QVERIFY(parseNumber("1.240", &v, Decimals::Comma)); QCOMPARE(v, 1240.0);
        QVERIFY(!parseNumber("abc", &v));
        QVERIFY(!parseNumber("", &v));
        QVERIFY(!parseNumber("4-2", &v));
        Cents c = 0;
        QVERIFY(parseMoney("19.99", &c)); QCOMPARE(c, Cents(1999));
    }

    void moneyIsWrittenInTheLanguage()
    {
        QCOMPARE(formatMoney(123456789, "$", QLocale(QLocale::English, QLocale::UnitedStates)), QString("$1,234,567.89"));
        QVERIFY(formatMoney(123456789, "€", QLocale(QLocale::German, QLocale::Germany)).startsWith("1.234.567,89"));
        QCOMPARE(formatPercent(12.345, QLocale(QLocale::English, QLocale::UnitedStates)), QString("12.3%"));
        QCOMPARE(formatPercent(-0.01, QLocale(QLocale::English, QLocale::UnitedStates)), QString("0.0%"));
    }

    void takeoffsAreReadByTheirHeadings()
    {
        const Takeoff t = parseTakeoff("\xEF\xBB\xBF# Plumb takeoff\n"
                                       "Sheet,Qty,Description,UoM,Unit Price,Code\n"
                                       "S-101,42.5,Concrete footings,m3,\"1,250.00\",03-100\n"
                                       "S-102,10,Concrete footings,m3,\"1,250.00\",03-100\n"
                                       "A-301,1240,\"Partitions, gypsum\",m2,38.5,09-250\n"
                                       "E-101,x,Rough-in,ls,100,26-100\n"
                                       ",,,,,\n"
                                       ",,Total,,,\n");
        QVERIFY(t.error.isEmpty());
        QCOMPARE(t.rows.size(), 2);
        // Measured on two sheets, counted once.
        QCOMPARE(t.rows[0].code, QString("03-100"));
        QCOMPARE(t.rows[0].quantity, 52.5);
        QCOMPARE(t.rows[0].unitPrice, Cents(125000));
        QCOMPARE(t.rows[1].description, QString("Partitions, gypsum"));
        QCOMPARE(t.warnings.size(), 1);
        QVERIFY(t.warnings[0].contains("Line 6"));
        QVERIFY(!t.hasInstalled);
    }

    void takeoffsWithDecimalCommasAndSemicolons()
    {
        const Takeoff t = parseTakeoff("Item no.;Description;Qty;Unit;Installed to date\n"
                                       "1;Footings;52,5;m3;10,5\n"
                                       "2;Partitions;1.240;m2;496\n");
        QVERIFY(t.error.isEmpty());
        QVERIFY(t.hasInstalled);
        QCOMPARE(t.rows[0].quantity, 52.5);
        QCOMPARE(t.rows[1].quantity, 1240.0);
        QCOMPARE(t.rows[1].installed, 496.0);
    }

    void takeoffsWithoutHeadingsAreRefused()
    {
        QVERIFY(!parseTakeoff("a,b,c\n1,2,3\n").error.isEmpty());
        QVERIFY(!parseTakeoff("Description,Quantity\n").error.isEmpty());
    }

    void importsPlanEachLine()
    {
        Project p;
        p.backend.setLineField(p.footings, "code", "03-100");
        const Takeoff t = parseTakeoff("Code,Description,Quantity,Unit,Unit price,Installed\n"
                                       "03-100,Footings,40,m3,500,30\n"
                                       "09-900,Painting,2000,m2,4,0\n"
                                       ",Structural steel,0,,,\n");
        ImportPlan plan = planImport(p.backend.data(), t, 0);
        QCOMPARE(plan.updates, 1);
        QCOMPARE(plan.additions, 1);
        // Steel is already as the takeoff has it.
        QCOMPARE(plan.unchanged, 1);
        QCOMPARE(plan.changes[2].action, ImportChange::Same);
        QCOMPARE(plan.changes[0].line.scheduled, dollars(20000));
        QVERIFY(!plan.changes[0].scheduleChanged || plan.changes[0].line.quantity == 40);
        QCOMPARE(plan.changes[1].line.scheduled, dollars(8000));
        // Without an application, installed quantities wait.
        QVERIFY(!plan.changes[0].billing);
        QVERIFY(plan.changes[0].note.contains("start an application"));

        p.backend.startPeriod();
        plan = planImport(p.backend.data(), t, p.backend.currentPeriod());
        QVERIFY(plan.changes[0].billing);
        QCOMPARE(plan.changes[0].percent, 75.0);
        QCOMPARE(plan.changes[0].completed, dollars(15000));
    }

    void importingIntoALockedScheduleSkipsNewItems()
    {
        Project p;
        p.backend.setLineField(p.steel, "code", "05-200");
        p.backend.startPeriod();
        p.backend.submitPeriod();
        p.backend.startPeriod();
        const Takeoff t = parseTakeoff("Code,Description,Quantity,Unit,Unit price,Installed\n"
                                       "05-200,Structural steel,20,t,3000,25\n"
                                       "09-900,Painting,2000,m2,4,0\n");
        const ImportPlan plan = planImport(p.backend.data(), t, p.backend.currentPeriod());
        QCOMPARE(plan.skipped, 1);
        QCOMPARE(plan.changes[0].action, ImportChange::Update);
        QVERIFY(!plan.changes[0].scheduleChanged);
        QVERIFY(plan.changes[0].note.contains("locked"));
        // Measured against the takeoff's quantity, but billed on the line's
        // fixed value, and never past it.
        QVERIFY(plan.changes[0].billing);
        QCOMPARE(plan.changes[0].completed, dollars(50000));
    }

    void backendAppliesAnImportAsOneStep()
    {
        Project p;
        const int co = p.backend.addChangeOrder();
        const QString file = p.write("takeoff.csv", "Description,Quantity,Unit,Rate\n"
                                                     "Finishes,1000,m2,35\n"
                                                     "Roofing,500,m2,60\n");
        QVERIFY(p.backend.importTakeoffFrom(file));
        QCOMPARE(p.backend.importPreview().value("additions").toInt(), 1);
        QCOMPARE(p.backend.importPreview().value("updates").toInt(), 1);
        QVERIFY(p.backend.applyImport());
        QVERIFY(p.backend.importPreview().isEmpty());
        const ProjectData data = p.backend.data();
        QCOMPARE(data.lines.size(), 5);
        QCOMPARE(data.lines[3].description, QString("Roofing"));
        QCOMPARE(data.lines[3].scheduled, dollars(30000));
        QCOMPARE(data.lines[4].id, co);
        QCOMPARE(data.line(p.finishes)->scheduled, dollars(35000));
        QVERIFY(p.backend.notice().contains("takeoff.csv"));
        p.backend.undo();
        QCOMPARE(p.backend.data().lines.size(), 4);
        QCOMPARE(p.backend.data().line(p.finishes)->scheduled, dollars(30000));

        QVERIFY(!p.backend.importTakeoffFrom(p.write("empty.csv", "")));
        QVERIFY(p.backend.error().contains("empty.csv"));
    }

    void payApplicationIsWrittenWhole()
    {
        Project p;
        QVERIFY(!p.backend.exportPayApplicationTo(p.dir.filePath("none.pdf")));
        p.backend.startPeriod();
        for (int i = 0; i < 40; ++i)
            p.addLine(QString("Line %1").arg(i), "1000");
        p.backend.setPercent(p.steel, "40");
        QVERIFY(p.backend.exportPayApplicationTo(p.dir.filePath("app")));
        QFile pdf(p.dir.filePath("app.pdf"));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        const QByteArray data = pdf.readAll();
        QVERIFY(data.startsWith("%PDF"));
        // The certificate, then the continuation sheet over two pages.
        QCOMPARE(data.count("/Type /Page\n") + data.count("/Type /Page "), qsizetype(3));
        QString error;
        QVERIFY(!writePayApplication(p.backend.data(), p.backend.currentPeriod(), "/nonexistent/dir/x.pdf", &error));
        QVERIFY(!error.isEmpty());
    }

    void commandsWorkWithoutAWindow()
    {
        QTemporaryDir dir;
        const QString project = dir.filePath("cli.spire");
        auto run = [](QStringList arguments) { return runCli(QStringList{"spire"} + arguments); };
        QCOMPARE(run({"new", project, "--name", "Depot", "--owner", "City", "--retainage", "5"}), 0);
        QCOMPARE(run({"add-line", project, "--description", "Slab", "--scheduled", "10000", "--code", "A1"}), 0);
        QCOMPARE(run({"add-line", project, "--scheduled", "5"}), 1);
        QCOMPARE(run({"bill", project, "A1", "--percent", "10"}), 1);
        QCOMPARE(run({"start", project, "--period-to", "2026-05-31"}), 0);
        QCOMPARE(run({"bill", project, "A1", "--percent", "50"}), 0);
        QCOMPARE(run({"bill", project, "nope", "--percent", "50"}), 1);
        QCOMPARE(run({"change-order", project, "--description", "Extra", "--amount", "2000", "--approve"}), 0);
        QCOMPARE(run({"bill", project, "CO1", "--complete"}), 0);
        QCOMPARE(run({"export", project, dir.filePath("out.pdf")}), 0);
        QCOMPARE(run({"submit", project}), 0);
        QCOMPARE(run({"submit", project}), 1);
        QCOMPARE(run({"bogus"}), 1);

        Store store;
        QVERIFY(store.open(project));
        const ProjectData data = store.load();
        QCOMPARE(data.info.owner, QString("City"));
        QCOMPARE(data.periods.size(), 1);
        QVERIFY(data.periods[0].submitted);
        QCOMPARE(data.periods[0].retainageWork, 5.0);
        const BillingSummary s = computeBilling(data, data.periods[0].id).summary;
        QCOMPARE(s.completedToDate, dollars(7000));
        QCOMPARE(s.currentDue, dollars(6650));
        QVERIFY(QFile::exists(dir.filePath("out.pdf")));
        // Commands leave the window's settings alone.
        QVERIFY(QSettings().value("lastProject").toString() != project);
    }

    void themeReadsPaletteColors()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\nred = \"#aa0000\"\ngreen = \"#00aa00\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.danger(), QString("#aa0000"));
        QCOMPARE(theme.success(), QString("#00aa00"));
        QCOMPARE(theme.warning(), QString("#F5A524"));
    }

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        write(dir.path() + "/theme/colors.toml", "accent = \"#112233\"\n");
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n");
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        write(response, "{\"int\":0}");
        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        write(response, "{\"int\":6}");
        QTRY_COMPARE(theme.radius(), 6);
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

    // Every shipped catalog is valid JSON, and keeps each English string's
    // placeholders, so .arg() fills them in every language.
    void catalogsKeepTheirPlaceholders()
    {
        const QStringList catalogs = QDir(":/i18n").entryList({"*.json"});
        QVERIFY(catalogs.size() >= 6);
        static const QRegularExpression placeholder("%(\\d|n)");
        for (const auto &name : catalogs) {
            QFile file(":/i18n/" + name);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QJsonParseError error;
            const QJsonObject object = QJsonDocument::fromJson(file.readAll(), &error).object();
            QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(name + ": " + error.errorString()));
            for (auto it = object.begin(); it != object.end(); ++it) {
                QSet<QString> wanted;
                for (auto m = placeholder.globalMatch(it.key()); m.hasNext();)
                    wanted << m.next().captured();
                const QStringList forms = it.value().isObject() ? QStringList(it.value().toObject().toVariantMap().keys())
                                                                : QStringList{QString()};
                for (const auto &form : forms) {
                    const QString text = form.isEmpty() ? it.value().toString() : it.value().toObject().value(form).toString();
                    QSet<QString> found;
                    for (auto m = placeholder.globalMatch(text); m.hasNext();)
                        found << m.next().captured();
                    QVERIFY2(found == wanted, qPrintable(name + ": " + it.key()));
                }
            }
        }
    }
};

int runSpireTests(int argc, char **argv)
{
    SpireTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "spire_tests.moc"
