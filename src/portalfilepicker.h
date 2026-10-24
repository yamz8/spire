#pragma once

#include <QVariantMap>

#include "filepicker.h"

class QDBusPendingCallWatcher;

// The desktop's own file chooser, through xdg-desktop-portal over D-Bus.
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);

    void open(const QString &purpose, const QString &title, const QList<FileFilter> &filters, bool multiple,
              const QString &folder) override;
    void save(const QString &purpose, const QString &title, const QString &suggestedPath,
              const FileFilter &filter) override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    void requestFile(const QString &method, const QString &title, QVariantMap options, const QString &purpose);
    bool connectToRequestPath(const QString &path);
    void clearPending();
    // Best-effort Close of the pending portal request, if there is one.
    void closePendingRequest();

    QString m_pendingPath;
    QString m_pendingPurpose;
    // Bumped per request, so a late reply for a replaced request is ignored.
    int m_pendingGeneration = 0;
    QDBusPendingCallWatcher *m_pendingWatcher = nullptr;
};
