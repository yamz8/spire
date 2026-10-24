#pragma once

#include <functional>

#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "linemodel.h"
#include "plumbimport.h"
#include "store.h"

class FilePicker;

// The open project as QML sees it: the schedule of values, its change
// orders, and the application on screen with its totals. Every change is
// written to the project file as it happens, so there is never anything
// unsaved, and every change can be undone.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY projectChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap info READ info NOTIFY dataChanged)
    Q_PROPERTY(QVariantList periods READ periods NOTIFY dataChanged)
    Q_PROPERTY(int currentPeriod READ currentPeriod WRITE setCurrentPeriod NOTIFY currentPeriodChanged)
    Q_PROPERTY(QVariantMap period READ period NOTIFY dataChanged)
    // Whether the application on screen can still be billed.
    Q_PROPERTY(bool editable READ editable NOTIFY dataChanged)
    Q_PROPERTY(bool sovLocked READ sovLocked NOTIFY dataChanged)
    Q_PROPERTY(bool canStartPeriod READ canStartPeriod NOTIFY dataChanged)
    Q_PROPERTY(LineModel *lines READ lines CONSTANT)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY dataChanged)
    Q_PROPERTY(QVariantList changeOrders READ changeOrders NOTIFY dataChanged)
    Q_PROPERTY(int pendingChangeOrders READ pendingChangeOrders NOTIFY dataChanged)
    Q_PROPERTY(bool hasQuantities READ hasQuantities NOTIFY dataChanged)
    Q_PROPERTY(int selectedLine READ selectedLine WRITE selectLine NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap importPreview READ importPreview NOTIFY importPreviewChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString lastExport READ lastExport NOTIFY noticeChanged)

public:
    explicit Backend(FilePicker *picker, QObject *parent = nullptr);
    ~Backend() override;

    bool isOpen() const { return m_store.isOpen(); }
    QString projectPath() const { return m_store.path(); }
    QVariantMap info() const;
    QVariantList periods() const;
    int currentPeriod() const { return m_currentPeriod; }
    void setCurrentPeriod(int id);
    QVariantMap period() const;
    bool editable() const;
    bool sovLocked() const { return m_data.sovLocked(); }
    bool canStartPeriod() const;
    LineModel *lines() { return &m_lines; }
    QVariantMap summary() const { return m_summary; }
    QVariantList changeOrders() const;
    int pendingChangeOrders() const;
    bool hasQuantities() const;
    int selectedLine() const { return m_selectedLine; }
    QVariantMap importPreview() const { return m_importPreview; }
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString error() const { return m_error; }
    QString notice() const { return m_notice; }
    QString lastExport() const { return m_lastExport; }
    const ProjectData &data() const { return m_data; }

    // Pickers: each asks the desktop's file chooser, then acts on the answer.
    Q_INVOKABLE void newProject();
    Q_INVOKABLE void openProject();
    Q_INVOKABLE void importTakeoff();
    Q_INVOKABLE void exportPayApplication();

    Q_INVOKABLE bool createProjectAt(const QString &path, bool remember = true);
    // Remembering the project (to reopen it next time) is for the window;
    // commands leave the settings alone.
    Q_INVOKABLE bool openProjectAt(const QString &path, bool remember = true);
    Q_INVOKABLE void reopenLastProject();
    Q_INVOKABLE bool importTakeoffFrom(const QString &path);
    Q_INVOKABLE bool applyImport();
    Q_INVOKABLE void cancelImport();
    Q_INVOKABLE bool exportPayApplicationTo(const QString &path);
    Q_INVOKABLE void openExport();

    // Project details: name, location, owner, contractor, architect,
    // contractFor, contractNumber, contractDate, currency, retainageWork,
    // retainageStored.
    Q_INVOKABLE bool setInfo(const QString &field, const QVariant &value);

    // The schedule of values. Values typed by hand come in as text and are
    // read the way the person wrote them.
    Q_INVOKABLE int addLine();
    Q_INVOKABLE bool setLineField(int id, const QString &field, const QString &value);
    Q_INVOKABLE bool removeLine(int id);
    Q_INVOKABLE bool moveLine(int id, int step);

    // Billing the application on screen.
    Q_INVOKABLE bool setPercent(int lineId, const QString &percent);
    Q_INVOKABLE bool setThisPeriod(int lineId, const QString &amount);
    Q_INVOKABLE bool setStored(int lineId, const QString &amount);
    Q_INVOKABLE bool setInstalled(int lineId, const QString &quantity);
    Q_INVOKABLE bool completeLine(int lineId);

    // Applications.
    Q_INVOKABLE bool startPeriod();
    Q_INVOKABLE bool setPeriodField(const QString &field, const QVariant &value);
    Q_INVOKABLE bool submitPeriod();
    Q_INVOKABLE bool reopenPeriod();
    Q_INVOKABLE bool removePeriod();
    Q_INVOKABLE void stepPeriod(int step);

    // Change orders.
    Q_INVOKABLE int addChangeOrder();
    Q_INVOKABLE bool setChangeOrderField(int id, const QString &field, const QVariant &value);
    Q_INVOKABLE bool setApproved(int id, bool approved);

    Q_INVOKABLE void selectLine(int id);
    Q_INVOKABLE void selectRelative(int step);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void clearNotice() { setNotice({}); }

    // Formatting for QML, in the language's own way.
    Q_INVOKABLE QString money(double cents) const;
    Q_INVOKABLE QString percent(double value) const;
    Q_INVOKABLE QString quantity(double value) const;
    Q_INVOKABLE QString date(const QVariant &value) const;

signals:
    void projectChanged();
    void dataChanged();
    void currentPeriodChanged();
    void selectionChanged();
    void importPreviewChanged();
    void historyChanged();
    void errorChanged();
    void noticeChanged();
    // Asks QML to start editing a new line or change order.
    void lineAdded(int id);
    void changeOrderAdded(int id);

private:
    struct Change {
        QJsonObject before;
        QJsonObject after;
        int period;
    };

    // Runs one change to the project as a single undoable step, in one
    // transaction; false (with the reason shown) when it could not be made.
    bool change(const std::function<bool()> &work);
    bool reject(const QString &message);
    void pickerSelected(const QString &purpose, const QList<QUrl> &urls);
    void setError(const QString &message);
    void setNotice(const QString &message);
    void reload();
    void showPeriod(int id);
    bool setEntry(int lineId, Entry entry);
    int baseLineCount() const;
    const Period *current() const { return m_data.period(m_currentPeriod); }
    QString folder() const;
    void rememberFolder(const QString &path);
    void closeProject();

    FilePicker *m_picker;
    Store m_store;
    ProjectData m_data;
    int m_currentPeriod = 0;
    int m_selectedLine = 0;
    LineModel m_lines;
    QVariantMap m_summary;
    QList<Change> m_undo;
    QList<Change> m_redo;
    Takeoff m_takeoff;
    QString m_takeoffPath;
    QVariantMap m_importPreview;
    QString m_error;
    QString m_notice;
    QString m_lastExport;
    bool m_remember = true;
};
