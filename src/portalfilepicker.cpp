// The portal file chooser follows Omacut (MIT, David Heinemeier Hansson).
#include "portalfilepicker.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>

namespace {
struct PortalFilterRule {
    uint type;
    QString pattern;
};

using PortalFilterRules = QList<PortalFilterRule>;

struct PortalFileFilter {
    QString name;
    PortalFilterRules rules;
};

using PortalFileFilters = QList<PortalFileFilter>;

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFilterRule &rule) {
    argument.beginStructure();
    argument << rule.type << rule.pattern;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFilterRule &rule) {
    argument.beginStructure();
    argument >> rule.type >> rule.pattern;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const PortalFileFilter &filter) {
    argument.beginStructure();
    argument << filter.name << filter.rules;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalFileFilter &filter) {
    argument.beginStructure();
    argument >> filter.name >> filter.rules;
    argument.endStructure();
    return argument;
}

void registerPortalFilterTypes() {
    static const bool registered = [] {
        qDBusRegisterMetaType<PortalFilterRule>();
        qDBusRegisterMetaType<PortalFilterRules>();
        qDBusRegisterMetaType<PortalFileFilter>();
        qDBusRegisterMetaType<PortalFileFilters>();
        return true;
    }();
    Q_UNUSED(registered);
}

// Rule type 0 is a glob, 1 a MIME type.
PortalFileFilter toPortal(const FileFilter &filter) {
    PortalFileFilter portal{filter.name, {}};
    for (const auto &mime : filter.mimeTypes)
        portal.rules.append({1, mime});
    for (const auto &pattern : filter.patterns)
        portal.rules.append({0, pattern});
    return portal;
}

QString portalToken() { return QStringLiteral("spire_%1").arg(QRandomGenerator::global()->generate()); }

QByteArray portalPathBytes(const QString &path) {
    QByteArray bytes = path.toUtf8();
    bytes.append('\0');
    return bytes;
}
}

PortalFilePicker::PortalFilePicker(QObject *parent) : FilePicker(parent) { registerPortalFilterTypes(); }

void PortalFilePicker::open(const QString &purpose, const QString &title, const QList<FileFilter> &filters,
                            bool multiple, const QString &folder) {
    PortalFileFilters portalFilters;
    for (const auto &filter : filters)
        portalFilters.append(toPortal(filter));

    QVariantMap options;
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("multiple"), multiple);
    options.insert(QStringLiteral("current_folder"),
                   portalPathBytes(QDir(folder).exists() ? folder : QDir::homePath()));
    if (!portalFilters.isEmpty()) {
        options.insert(QStringLiteral("filters"), QVariant::fromValue(portalFilters));
        options.insert(QStringLiteral("current_filter"), QVariant::fromValue(portalFilters.first()));
    }
    requestFile(QStringLiteral("OpenFile"), title, options, purpose);
}

void PortalFilePicker::save(const QString &purpose, const QString &title, const QString &suggestedPath,
                            const FileFilter &filter) {
    const QFileInfo target(suggestedPath);
    const PortalFileFilter portalFilter = toPortal(filter);

    QVariantMap options;
    options.insert(QStringLiteral("modal"), true);
    options.insert(QStringLiteral("current_folder"),
                   portalPathBytes(target.absoluteDir().exists() ? target.absolutePath() : QDir::homePath()));
    options.insert(QStringLiteral("current_name"), target.fileName());
    options.insert(QStringLiteral("filters"), QVariant::fromValue(PortalFileFilters{portalFilter}));
    options.insert(QStringLiteral("current_filter"), QVariant::fromValue(portalFilter));
    requestFile(QStringLiteral("SaveFile"), title, options, purpose);
}

bool PortalFilePicker::connectToRequestPath(const QString &path) {
    m_pendingPath = path;
    return QDBusConnection::sessionBus().connect(
        QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath, QStringLiteral("org.freedesktop.portal.Request"),
        QStringLiteral("Response"), this, SLOT(handleResponse(uint,QVariantMap)));
}

void PortalFilePicker::closePendingRequest() {
    if (m_pendingPath.isEmpty())
        return;
    QDBusInterface request(QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
                           QStringLiteral("org.freedesktop.portal.Request"), QDBusConnection::sessionBus());
    request.asyncCall(QStringLiteral("Close"));
}

void PortalFilePicker::requestFile(const QString &method, const QString &title, QVariantMap options,
                                   const QString &purpose) {
    // A request whose Response never arrives (the portal crashed or was
    // killed with the dialog up) must not wedge every later dialog behind
    // it: close the stuck one best-effort and replace it.
    if (!m_pendingPurpose.isEmpty()) {
        closePendingRequest();
        clearPending();
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface portal(QStringLiteral("org.freedesktop.portal.Desktop"),
                          QStringLiteral("/org/freedesktop/portal/desktop"),
                          QStringLiteral("org.freedesktop.portal.FileChooser"), bus);
    if (!portal.isValid()) {
        emit failed(tr("The desktop's file chooser (xdg-desktop-portal) is not available."));
        return;
    }

    // Subscribe to the Response signal at the request path the portal will
    // derive from our handle_token *before* making the call, so a response
    // can't slip past while our match rule is still being installed.
    const QString token = portalToken();
    options.insert(QStringLiteral("handle_token"), token);
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString predictedPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    m_pendingPurpose = purpose;
    if (!connectToRequestPath(predictedPath)) {
        clearPending();
        emit failed(tr("Could not listen for the file chooser's answer."));
        return;
    }

    const int generation = ++m_pendingGeneration;
    auto *watcher = new QDBusPendingCallWatcher(portal.asyncCall(method, QString(), title, options), this);
    m_pendingWatcher = watcher;

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        if (m_pendingWatcher == watcher)
            m_pendingWatcher = nullptr;

        // Superseded by a newer request, or a response may already have
        // been handled while the reply was in flight.
        if (generation != m_pendingGeneration || m_pendingPurpose.isEmpty())
            return;

        if (reply.isError()) {
            clearPending();
            emit failed(tr("The file chooser failed: %1").arg(reply.error().message()));
            return;
        }

        // Old portal versions can hand back a different request path than the
        // handle_token predicts; move our subscription over if so.
        const QString actualPath = reply.value().path();
        if (actualPath != m_pendingPath) {
            QDBusConnection::sessionBus().disconnect(
                QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
                QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
                SLOT(handleResponse(uint,QVariantMap)));
            if (!connectToRequestPath(actualPath)) {
                clearPending();
                emit failed(tr("Could not listen for the file chooser's answer."));
            }
        }
    });
}

void PortalFilePicker::handleResponse(uint response, const QVariantMap &results) {
    const QString purpose = m_pendingPurpose;
    clearPending();
    if (response != 0 || purpose.isEmpty())
        return;

    QList<QUrl> urls;
    for (const auto &uri : results.value(QStringLiteral("uris")).toStringList())
        urls << QUrl(uri);
    if (!urls.isEmpty())
        emit selected(purpose, urls);
}

void PortalFilePicker::clearPending() {
    // A caller that never gets its reply must not outlive the request it
    // belonged to (repeated stuck requests would otherwise pile them up).
    if (m_pendingWatcher) {
        m_pendingWatcher->deleteLater();
        m_pendingWatcher = nullptr;
    }
    if (!m_pendingPath.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(
            QStringLiteral("org.freedesktop.portal.Desktop"), m_pendingPath,
            QStringLiteral("org.freedesktop.portal.Request"), QStringLiteral("Response"), this,
            SLOT(handleResponse(uint,QVariantMap)));
    }
    m_pendingPath.clear();
    m_pendingPurpose.clear();
}
