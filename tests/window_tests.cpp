#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "billing.h"
#include "fakepicker.h"
#include "theme.h"
#include "translator.h"

// The real window, offscreen, on a project with three lines and its first
// application open, in a language of the test's choosing.
struct Window {
    QTemporaryDir dir;
    Translator *translator = nullptr;
    Theme *theme = nullptr;
    FakePicker picker;
    Backend *backend = nullptr;
    QQmlApplicationEngine *engine = nullptr;
    QQuickWindow *window = nullptr;
    int footings = 0, steel = 0, finishes = 0;

    explicit Window(const QByteArray &language = "en", bool started = true) {
        QFile file(dir.filePath("language"));
        file.open(QIODevice::WriteOnly);
        file.write(language);
        file.close();
        translator = new Translator(dir.filePath("language"));
        QCoreApplication::installTranslator(translator);
        theme = new Theme(dir.path());
        backend = new Backend(&picker);

        backend->createProjectAt(dir.filePath("job.spire"), false);
        auto add = [this](const char *description, const char *amount) {
            const int id = backend->addLine();
            backend->setLineField(id, "description", description);
            backend->setLineField(id, "scheduled", amount);
            return id;
        };
        footings = add("Footings", "20000");
        steel = add("Structural steel", "50000");
        finishes = add("Finishes", "30000");
        if (started)
            backend->startPeriod();
        backend->selectLine(0);
        backend->clearNotice();

        engine = new QQmlApplicationEngine;
        QObject::connect(translator, &Translator::changed, engine, &QQmlEngine::retranslate);
        engine->rootContext()->setContextProperty("translator", translator);
        engine->rootContext()->setContextProperty("theme", theme);
        engine->rootContext()->setContextProperty("backend", backend);
        engine->load(QUrl("qrc:/Main.qml"));
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
        if (window) {
            window->resize(1480, 900);
            QTest::qWaitForWindowExposed(window);
            QTest::qWait(100);
        }
    }
    ~Window() {
        delete engine;
        delete backend;
        delete theme;
        QCoreApplication::removeTranslator(translator);
        delete translator;
    }

    // Delegates and popups sit in the visual item tree but not always under
    // the window as QObjects, so walk the items; only visible ones count.
    static QQuickItem *search(QQuickItem *item, const QString &name) {
        if (item->objectName() == name && item->isVisible())
            return item;
        for (auto *child : item->childItems())
            if (auto *found = search(child, name))
                return found;
        return nullptr;
    }
    QQuickItem *find(const QString &name) const { return search(window->contentItem(), name); }
    QPoint center(QQuickItem *item) const { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); }
    void settle() { QTest::qWait(100); }
    void click(const QPoint &point) { QTest::mouseClick(window, Qt::LeftButton, {}, point); }
    void click(const QString &name) {
        QQuickItem *item = nullptr;
        QTRY_VERIFY2((item = find(name)), qPrintable("no " + name));
        settle();
        click(center(item));
    }
    void type(const QByteArray &text) {
        for (char c : text)
            QTest::keyClick(window, c);
    }
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = {}) { QTest::keyClick(window, key, modifiers); }
    BillingRow row(int id) const {
        for (const auto &row : computeBilling(backend->data(), backend->currentPeriod()).rows)
            if (row.line.id == id)
                return row;
        return {};
    }
};

class WindowTests : public QObject {
    Q_OBJECT

private slots:
    void init() { QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates)); }

    void spaceEditsPercentAndEnterMovesDown()
    {
        Window w;
        QVERIFY(w.window);
        w.key(Qt::Key_J);
        w.key(Qt::Key_J);
        QCOMPARE(w.backend->selectedLine(), w.steel);
        w.key(Qt::Key_Space);
        QTRY_VERIFY(w.find("cellEditor") && w.find("cellEditor")->hasActiveFocus());
        w.type("40");
        w.key(Qt::Key_Return);
        QCOMPARE(w.row(w.steel).percentComplete, 40.0);
        // The editor moved to the next line's percent.
        QTRY_COMPARE(w.backend->selectedLine(), w.finishes);
        QTRY_VERIFY(w.find("cellEditor"));
        w.type("10");
        w.key(Qt::Key_Escape);
        QCOMPARE(w.row(w.finishes).percentComplete, 0.0);
        QVERIFY(!w.find("cellEditor"));
        // Escape again deselects, and single keys are shortcuts again.
        w.key(Qt::Key_Escape);
        QCOMPARE(w.backend->selectedLine(), 0);
    }

    void tabMovesAcrossTheEditableCells()
    {
        Window w;
        w.backend->selectLine(w.footings);
        w.key(Qt::Key_Space);
        QTRY_VERIFY(w.find("cellEditor"));
        w.type("50");
        w.key(Qt::Key_Tab);
        // From percent to this period, which now holds what 50% bills.
        QTRY_VERIFY(w.find("cellEditor"));
        QCOMPARE(w.find("cellEditor")->property("text").toString(), QString("10000.00"));
        w.type("5000");
        w.key(Qt::Key_Tab);
        QCOMPARE(w.row(w.footings).thisPeriod, Cents(500000));
        QTRY_VERIFY(w.find("cellEditor"));
        w.type("2500");
        w.key(Qt::Key_Return);
        QCOMPARE(w.row(w.footings).stored, Cents(250000));
        QTRY_COMPARE(w.find("currentDueAmount")->property("text").toString(), QString("$6,750.00"));
    }

    void clickingACellEditsIt()
    {
        Window w;
        auto *list = w.find("lineList");
        QVERIFY(list);
        // The description of the first line, a little way into its column.
        const QPoint point = list->mapToScene(QPointF(150, 20)).toPoint();
        w.click(point);
        QTRY_VERIFY(w.find("cellEditor"));
        QCOMPARE(w.find("cellEditor")->property("text").toString(), QString("Footings"));
        // The text starts selected, so typing replaces it; End keeps it.
        w.key(Qt::Key_End);
        w.type(" and piers");
        w.key(Qt::Key_Return);
        QCOMPARE(w.backend->data().line(w.footings)->description, QString("Footings and piers"));
    }

    void fShortcutBillsInFullAndUndoTakesItBack()
    {
        Window w;
        w.backend->selectLine(w.steel);
        w.key(Qt::Key_F);
        QCOMPARE(w.row(w.steel).percentComplete, 100.0);
        w.key(Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(w.row(w.steel).percentComplete, 0.0);
        w.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(w.row(w.steel).percentComplete, 100.0);
    }

    void submittingAsksFirst()
    {
        Window w;
        w.click("submitPeriod");
        QTRY_VERIFY(w.find("confirmButton"));
        QVERIFY(w.backend->editable());
        w.click("confirmButton");
        QTRY_VERIFY(!w.backend->editable());
        QVERIFY(w.backend->data().periods.first().submitted);
        // The next application can start straight away.
        w.key(Qt::Key_N);
        QCOMPARE(w.backend->data().periods.size(), 2);
        QVERIFY(w.backend->editable());
    }

    void changeOrdersAreAddedAndApproved()
    {
        Window w;
        w.key(Qt::Key_C);
        QTRY_VERIFY(w.find("changeOrderList"));
        QTRY_VERIFY(w.find("coDescription") && w.find("coDescription")->hasActiveFocus());
        w.type("Canopy");
        w.key(Qt::Key_Tab);
        const QList<Line> &lines = w.backend->data().lines;
        QCOMPARE(lines.last().description, QString("Canopy"));
        w.click("coApproved");
        QTRY_VERIFY(w.backend->data().lines.last().approved);
        QCOMPARE(w.backend->pendingChangeOrders(), 0);
        w.key(Qt::Key_1);
        QTRY_COMPARE(w.backend->lines()->count(), 4);
    }

    void importsArePreviewedFirst()
    {
        Window w;
        QFile csv(w.dir.filePath("takeoff.csv"));
        csv.open(QIODevice::WriteOnly);
        csv.write("Description,Quantity,Unit,Unit price,Installed\nFinishes,1000,m2,30,250\nRoofing,100,m2,50,0\n");
        csv.close();
        w.picker.answers = {csv.fileName()};
        w.key(Qt::Key_I);
        QTRY_VERIFY(w.find("applyImport"));
        QCOMPARE(w.backend->data().lines.size(), 3);
        w.click("applyImport");
        QTRY_COMPARE(w.backend->data().lines.size(), 4);
        QCOMPARE(w.row(w.finishes).percentComplete, 25.0);
        QTRY_VERIFY(!w.find("applyImport"));
    }

    void exportSavesThePdf()
    {
        Window w;
        w.picker.answers = {w.dir.filePath("application.pdf")};
        w.key(Qt::Key_E, Qt::ControlModifier);
        QTRY_VERIFY(QFile::exists(w.dir.filePath("application.pdf")));
        QTRY_VERIFY(w.find("noticeBar"));
    }

    void projectDetailsAreEditedInAPopup()
    {
        Window w;
        w.key(Qt::Key_Comma, Qt::ControlModifier);
        QTRY_VERIFY(w.window->activeFocusItem() && w.window->activeFocusItem()->property("text").toString() == "job");
        QTest::keyClick(w.window, Qt::Key_A, Qt::ControlModifier);
        w.type("Harbor Street");
        w.key(Qt::Key_Tab);
        w.type("12 Pier Road");
        w.key(Qt::Key_Tab);
        QCOMPARE(w.backend->data().info.name, QString("Harbor Street"));
        QCOMPARE(w.backend->data().info.location, QString("12 Pier Road"));
        QCOMPARE(w.window->title(), QString("Harbor Street — Spire"));
        // Escape closes the popup, and keys are shortcuts again.
        w.key(Qt::Key_Escape);
        QTRY_VERIFY(!w.window->activeFocusItem() || !w.window->activeFocusItem()->property("cursorPosition").isValid());
        w.key(Qt::Key_J);
        QCOMPARE(w.backend->selectedLine(), w.footings);
    }

    void refusalsAreSaidInTheWindow()
    {
        Window w;
        w.backend->setPercent(w.steel, "150");
        QTRY_VERIFY(w.find("errorBar"));
    }

    void anEmptyProjectOffersTheFirstSteps()
    {
        Window w("en", false);
        QVERIFY(w.find("startPeriod"));
        QVERIFY(!w.find("submitPeriod"));
        QVERIFY(!w.find("exportButton")->isEnabled());
    }

    void rightToLeftLanguagesMirrorTheWindow()
    {
        Window w("he");
        QVERIFY(w.center(w.find("summary")).x() < w.center(w.find("sheet")).x());
    }

    void englishWindowKeepsTheSummaryOnTheRight()
    {
        Window w;
        QVERIFY(w.center(w.find("summary")).x() > w.center(w.find("sheet")).x());
    }
};

int runWindowTests(int argc, char **argv)
{
    WindowTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "window_tests.moc"
