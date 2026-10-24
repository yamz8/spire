#include "store.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>

namespace {
constexpr int SchemaVersion = 1;

QString tr(const char *text) { return QCoreApplication::translate("Store", text); }

QSqlDatabase database(const QString &connection) { return QSqlDatabase::database(connection, false); }

QString isoDate(const QDate &date) { return date.isValid() ? date.toString(Qt::ISODate) : QString(); }
QDate fromIso(const QString &text) { return QDate::fromString(text, Qt::ISODate); }

const QStringList Schema = {
    "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)",
    "CREATE TABLE lines (id INTEGER PRIMARY KEY, position INTEGER NOT NULL, kind INTEGER NOT NULL DEFAULT 0,"
    " code TEXT, description TEXT, scheduled INTEGER NOT NULL DEFAULT 0, quantity REAL NOT NULL DEFAULT 0,"
    " unit TEXT, unit_price INTEGER NOT NULL DEFAULT 0, co_number INTEGER NOT NULL DEFAULT 0, co_date TEXT,"
    " approved INTEGER NOT NULL DEFAULT 0, approved_in INTEGER NOT NULL DEFAULT 0)",
    "CREATE TABLE periods (id INTEGER PRIMARY KEY, number INTEGER NOT NULL UNIQUE, period_to TEXT,"
    " application_date TEXT, retainage_work REAL NOT NULL, retainage_stored REAL NOT NULL,"
    " submitted INTEGER NOT NULL DEFAULT 0)",
    "CREATE TABLE entries (period_id INTEGER NOT NULL REFERENCES periods(id) ON DELETE CASCADE,"
    " line_id INTEGER NOT NULL REFERENCES lines(id) ON DELETE CASCADE, completed INTEGER NOT NULL DEFAULT 0,"
    " stored INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (period_id, line_id))",
};

const QStringList InfoKeys = {"name",       "location",       "owner",         "contractor",     "architect",
                              "contractFor", "contractNumber", "contractDate", "currency", "retainageWork",
                              "retainageStored"};

QHash<QString, QString> infoToMap(const ProjectInfo &info) {
    return {{"name", info.name},
            {"location", info.location},
            {"owner", info.owner},
            {"contractor", info.contractor},
            {"architect", info.architect},
            {"contractFor", info.contractFor},
            {"contractNumber", info.contractNumber},
            {"contractDate", isoDate(info.contractDate)},
            {"currency", info.currency},
            {"retainageWork", QString::number(info.retainageWork)},
            {"retainageStored", QString::number(info.retainageStored)}};
}

ProjectInfo infoFromMap(const QHash<QString, QString> &map) {
    ProjectInfo info;
    info.name = map.value("name");
    info.location = map.value("location");
    info.owner = map.value("owner");
    info.contractor = map.value("contractor");
    info.architect = map.value("architect");
    info.contractFor = map.value("contractFor");
    info.contractNumber = map.value("contractNumber");
    info.contractDate = fromIso(map.value("contractDate"));
    info.currency = map.value("currency", "$");
    info.retainageWork = map.value("retainageWork", "10").toDouble();
    info.retainageStored = map.value("retainageStored", "10").toDouble();
    return info;
}

QJsonObject lineToJson(const Line &line) {
    return {{"id", line.id},
            {"position", line.position},
            {"kind", line.kind},
            {"code", line.code},
            {"description", line.description},
            {"scheduled", line.scheduled},
            {"quantity", line.quantity},
            {"unit", line.unit},
            {"unitPrice", line.unitPrice},
            {"coNumber", line.coNumber},
            {"coDate", isoDate(line.coDate)},
            {"approved", line.approved},
            {"approvedIn", line.approvedIn}};
}

Line lineFromJson(const QJsonObject &object) {
    Line line;
    line.id = object.value("id").toInt();
    line.position = object.value("position").toInt();
    line.kind = object.value("kind").toInt();
    line.code = object.value("code").toString();
    line.description = object.value("description").toString();
    line.scheduled = object.value("scheduled").toInteger();
    line.quantity = object.value("quantity").toDouble();
    line.unit = object.value("unit").toString();
    line.unitPrice = object.value("unitPrice").toInteger();
    line.coNumber = object.value("coNumber").toInt();
    line.coDate = fromIso(object.value("coDate").toString());
    line.approved = object.value("approved").toBool();
    line.approvedIn = object.value("approvedIn").toInt();
    return line;
}

QJsonObject periodToJson(const Period &period) {
    return {{"id", period.id},
            {"number", period.number},
            {"periodTo", isoDate(period.periodTo)},
            {"applicationDate", isoDate(period.applicationDate)},
            {"retainageWork", period.retainageWork},
            {"retainageStored", period.retainageStored},
            {"submitted", period.submitted}};
}

Period periodFromJson(const QJsonObject &object) {
    Period period;
    period.id = object.value("id").toInt();
    period.number = object.value("number").toInt();
    period.periodTo = fromIso(object.value("periodTo").toString());
    period.applicationDate = fromIso(object.value("applicationDate").toString());
    period.retainageWork = object.value("retainageWork").toDouble();
    period.retainageStored = object.value("retainageStored").toDouble();
    period.submitted = object.value("submitted").toBool();
    return period;
}

void bindLine(QSqlQuery &query, const Line &line) {
    query.bindValue(":position", line.position);
    query.bindValue(":kind", line.kind);
    query.bindValue(":code", line.code);
    query.bindValue(":description", line.description);
    query.bindValue(":scheduled", line.scheduled);
    query.bindValue(":quantity", line.quantity);
    query.bindValue(":unit", line.unit);
    query.bindValue(":unit_price", line.unitPrice);
    query.bindValue(":co_number", line.coNumber);
    query.bindValue(":co_date", isoDate(line.coDate));
    query.bindValue(":approved", line.approved ? 1 : 0);
    query.bindValue(":approved_in", line.approvedIn);
}

void bindPeriod(QSqlQuery &query, const Period &period) {
    query.bindValue(":number", period.number);
    query.bindValue(":period_to", isoDate(period.periodTo));
    query.bindValue(":application_date", isoDate(period.applicationDate));
    query.bindValue(":retainage_work", period.retainageWork);
    query.bindValue(":retainage_stored", period.retainageStored);
    query.bindValue(":submitted", period.submitted ? 1 : 0);
}
}

const Period *ProjectData::period(int id) const {
    for (const auto &period : periods)
        if (period.id == id)
            return &period;
    return nullptr;
}

const Period *ProjectData::periodNumber(int number) const {
    for (const auto &period : periods)
        if (period.number == number)
            return &period;
    return nullptr;
}

const Period *ProjectData::previous(const Period &current) const {
    const Period *best = nullptr;
    for (const auto &period : periods)
        if (period.number < current.number && (!best || period.number > best->number))
            best = &period;
    return best;
}

const Line *ProjectData::line(int id) const {
    for (const auto &line : lines)
        if (line.id == id)
            return &line;
    return nullptr;
}

bool ProjectData::sovLocked() const {
    return std::any_of(periods.begin(), periods.end(), [](const Period &p) { return p.submitted; });
}

bool ProjectData::billedOnSubmitted(int lineId) const {
    for (const auto &period : periods) {
        const Entry e = entry(period.id, lineId);
        if (period.submitted && (e.completed || e.stored))
            return true;
    }
    return false;
}

int ProjectData::nextChangeOrderNumber() const {
    int number = 0;
    for (const auto &line : lines)
        number = std::max(number, line.coNumber);
    return number + 1;
}

Store::Store() = default;

Store::~Store() { close(); }

bool Store::fail(const QString &message) const {
    m_error = message;
    return false;
}

bool Store::exec(const QString &statement) const {
    QSqlQuery query(database(m_connection));
    if (!query.exec(statement))
        return fail(tr("Could not save the project: %1").arg(query.lastError().text()));
    return true;
}

bool Store::connect(const QString &path) {
    close();
    const QString connection = "spire-" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
    db.setDatabaseName(path);
    if (!db.open()) {
        const QString message = db.lastError().text();
        db = {};
        QSqlDatabase::removeDatabase(connection);
        return fail(tr("Could not open %1: %2").arg(path, message));
    }
    m_connection = connection;
    m_path = QFileInfo(path).absoluteFilePath();
    QSqlQuery(db).exec("PRAGMA foreign_keys = ON");
    return true;
}

void Store::close() {
    if (m_connection.isEmpty())
        return;
    {
        auto db = database(m_connection);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
    m_connection.clear();
    m_path.clear();
}

bool Store::create(const QString &path, const ProjectInfo &info) {
    if (QFileInfo::exists(path))
        return fail(tr("%1 already exists.").arg(path));
    if (!connect(path))
        return false;
    auto db = database(m_connection);
    db.transaction();
    QSqlQuery query(db);
    for (const auto &statement : Schema + QStringList{QString("PRAGMA user_version = %1").arg(SchemaVersion)})
        if (!query.exec(statement)) {
            const QString message = query.lastError().text();
            db.rollback();
            close();
            QFile::remove(path);
            return fail(tr("Could not create %1: %2").arg(path, message));
        }
    db.commit();
    return setInfo(info);
}

bool Store::open(const QString &path) {
    if (!QFileInfo(path).isFile())
        return fail(tr("%1 does not exist.").arg(path));
    if (!connect(path))
        return false;
    QSqlQuery query(database(m_connection));
    if (!query.exec("PRAGMA user_version") || !query.next() || query.value(0).toInt() < 1 ||
        !QSqlQuery(database(m_connection)).exec("SELECT 1 FROM lines LIMIT 1")) {
        close();
        return fail(tr("%1 is not a Spire project.").arg(path));
    }
    if (query.value(0).toInt() > SchemaVersion) {
        close();
        return fail(tr("%1 was made by a newer Spire.").arg(path));
    }
    return true;
}

ProjectData Store::load() const {
    ProjectData data;
    auto db = database(m_connection);

    QHash<QString, QString> meta;
    QSqlQuery query(db);
    query.exec("SELECT key, value FROM meta");
    while (query.next())
        meta.insert(query.value(0).toString(), query.value(1).toString());
    data.info = infoFromMap(meta);
    if (data.info.name.isEmpty())
        data.info.name = QFileInfo(m_path).completeBaseName();

    query.exec("SELECT * FROM lines ORDER BY position, id");
    while (query.next()) {
        Line line;
        line.id = query.value("id").toInt();
        line.position = query.value("position").toInt();
        line.kind = query.value("kind").toInt();
        line.code = query.value("code").toString();
        line.description = query.value("description").toString();
        line.scheduled = query.value("scheduled").toLongLong();
        line.quantity = query.value("quantity").toDouble();
        line.unit = query.value("unit").toString();
        line.unitPrice = query.value("unit_price").toLongLong();
        line.coNumber = query.value("co_number").toInt();
        line.coDate = fromIso(query.value("co_date").toString());
        line.approved = query.value("approved").toBool();
        line.approvedIn = query.value("approved_in").toInt();
        data.lines << line;
    }

    query.exec("SELECT * FROM periods ORDER BY number");
    while (query.next()) {
        Period period;
        period.id = query.value("id").toInt();
        period.number = query.value("number").toInt();
        period.periodTo = fromIso(query.value("period_to").toString());
        period.applicationDate = fromIso(query.value("application_date").toString());
        period.retainageWork = query.value("retainage_work").toDouble();
        period.retainageStored = query.value("retainage_stored").toDouble();
        period.submitted = query.value("submitted").toBool();
        data.periods << period;
    }

    query.exec("SELECT period_id, line_id, completed, stored FROM entries");
    while (query.next())
        data.entries.insert({query.value(0).toInt(), query.value(1).toInt()},
                            {query.value(2).toLongLong(), query.value(3).toLongLong()});
    return data;
}

bool Store::setInfo(const ProjectInfo &info) {
    QSqlQuery query(database(m_connection));
    query.prepare("INSERT OR REPLACE INTO meta (key, value) VALUES (:key, :value)");
    const auto map = infoToMap(info);
    for (const auto &key : InfoKeys) {
        query.bindValue(":key", key);
        query.bindValue(":value", map.value(key));
        if (!query.exec())
            return fail(tr("Could not save the project: %1").arg(query.lastError().text()));
    }
    return true;
}

int Store::insertLine(const Line &original) {
    Line line = original;
    QSqlQuery query(database(m_connection));
    if (!line.position) {
        query.exec("SELECT COALESCE(MAX(position), 0) + 1 FROM lines");
        line.position = query.next() ? query.value(0).toInt() : 1;
    }
    query.prepare(QString("INSERT INTO lines (%1position, kind, code, description, scheduled, quantity, unit,"
                          " unit_price, co_number, co_date, approved, approved_in) VALUES (%2:position, :kind, :code,"
                          " :description, :scheduled, :quantity, :unit, :unit_price, :co_number, :co_date, :approved,"
                          " :approved_in)")
                      .arg(line.id ? "id, " : "", line.id ? ":id, " : ""));
    if (line.id)
        query.bindValue(":id", line.id);
    bindLine(query, line);
    if (!query.exec()) {
        fail(tr("Could not save the line: %1").arg(query.lastError().text()));
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool Store::updateLine(const Line &line) {
    QSqlQuery query(database(m_connection));
    query.prepare("UPDATE lines SET position = :position, kind = :kind, code = :code, description = :description,"
                  " scheduled = :scheduled, quantity = :quantity, unit = :unit, unit_price = :unit_price,"
                  " co_number = :co_number, co_date = :co_date, approved = :approved, approved_in = :approved_in"
                  " WHERE id = :id");
    bindLine(query, line);
    query.bindValue(":id", line.id);
    if (!query.exec())
        return fail(tr("Could not save the line: %1").arg(query.lastError().text()));
    return true;
}

bool Store::removeLine(int id) {
    QSqlQuery query(database(m_connection));
    query.prepare("DELETE FROM lines WHERE id = :id");
    query.bindValue(":id", id);
    if (!query.exec())
        return fail(tr("Could not delete the line: %1").arg(query.lastError().text()));
    return true;
}

// Renumbers every line so the moved one sits at the given place (1-based)
// among them.
bool Store::moveLine(int id, int position) {
    QList<int> ids;
    QSqlQuery query(database(m_connection));
    query.exec("SELECT id FROM lines ORDER BY position, id");
    while (query.next())
        if (query.value(0).toInt() != id)
            ids << query.value(0).toInt();
    ids.insert(std::clamp(position - 1, 0, int(ids.size())), id);
    query.prepare("UPDATE lines SET position = :position WHERE id = :id");
    for (int i = 0; i < ids.size(); ++i) {
        query.bindValue(":position", i + 1);
        query.bindValue(":id", ids[i]);
        if (!query.exec())
            return fail(tr("Could not move the line: %1").arg(query.lastError().text()));
    }
    return true;
}

int Store::insertPeriod(const Period &period) {
    QSqlQuery query(database(m_connection));
    query.prepare(QString("INSERT INTO periods (%1number, period_to, application_date, retainage_work,"
                          " retainage_stored, submitted) VALUES (%2:number, :period_to, :application_date,"
                          " :retainage_work, :retainage_stored, :submitted)")
                      .arg(period.id ? "id, " : "", period.id ? ":id, " : ""));
    if (period.id)
        query.bindValue(":id", period.id);
    bindPeriod(query, period);
    if (!query.exec()) {
        fail(tr("Could not save the period: %1").arg(query.lastError().text()));
        return 0;
    }
    return query.lastInsertId().toInt();
}

bool Store::updatePeriod(const Period &period) {
    QSqlQuery query(database(m_connection));
    query.prepare("UPDATE periods SET number = :number, period_to = :period_to, application_date = :application_date,"
                  " retainage_work = :retainage_work, retainage_stored = :retainage_stored, submitted = :submitted"
                  " WHERE id = :id");
    bindPeriod(query, period);
    query.bindValue(":id", period.id);
    if (!query.exec())
        return fail(tr("Could not save the period: %1").arg(query.lastError().text()));
    return true;
}

bool Store::removePeriod(int id) {
    QSqlQuery query(database(m_connection));
    query.prepare("DELETE FROM periods WHERE id = :id");
    query.bindValue(":id", id);
    if (!query.exec())
        return fail(tr("Could not delete the period: %1").arg(query.lastError().text()));
    return true;
}

bool Store::setEntry(int periodId, int lineId, const Entry &entry) {
    QSqlQuery query(database(m_connection));
    if (!entry.completed && !entry.stored) {
        query.prepare("DELETE FROM entries WHERE period_id = :period AND line_id = :line");
    } else {
        query.prepare("INSERT OR REPLACE INTO entries (period_id, line_id, completed, stored)"
                      " VALUES (:period, :line, :completed, :stored)");
        query.bindValue(":completed", entry.completed);
        query.bindValue(":stored", entry.stored);
    }
    query.bindValue(":period", periodId);
    query.bindValue(":line", lineId);
    if (!query.exec())
        return fail(tr("Could not save the billing: %1").arg(query.lastError().text()));
    return true;
}

bool Store::begin() {
    if (!database(m_connection).transaction())
        return fail(tr("Could not save the project: %1").arg(database(m_connection).lastError().text()));
    return true;
}

bool Store::commit() {
    if (!database(m_connection).commit())
        return fail(tr("Could not save the project: %1").arg(database(m_connection).lastError().text()));
    return true;
}

void Store::rollback() { database(m_connection).rollback(); }

QJsonObject Store::snapshot() const {
    const ProjectData data = load();
    QJsonObject info;
    const auto map = infoToMap(data.info);
    for (auto it = map.begin(); it != map.end(); ++it)
        info.insert(it.key(), it.value());
    QJsonArray lines, periods, entries;
    for (const auto &line : data.lines)
        lines.append(lineToJson(line));
    for (const auto &period : data.periods)
        periods.append(periodToJson(period));
    for (auto it = data.entries.begin(); it != data.entries.end(); ++it)
        entries.append(QJsonArray{it.key().first, it.key().second, it.value().completed, it.value().stored});
    return {{"info", info}, {"lines", lines}, {"periods", periods}, {"entries", entries}};
}

bool Store::restore(const QJsonObject &snapshot) {
    if (!begin())
        return false;
    auto failed = [this] {
        rollback();
        return false;
    };
    if (!exec("DELETE FROM entries") || !exec("DELETE FROM periods") || !exec("DELETE FROM lines"))
        return failed();

    QHash<QString, QString> map;
    const QJsonObject info = snapshot.value("info").toObject();
    for (auto it = info.begin(); it != info.end(); ++it)
        map.insert(it.key(), it.value().toString());
    if (!setInfo(infoFromMap(map)))
        return failed();
    for (const auto &value : snapshot.value("lines").toArray())
        if (!insertLine(lineFromJson(value.toObject())))
            return failed();
    for (const auto &value : snapshot.value("periods").toArray())
        if (!insertPeriod(periodFromJson(value.toObject())))
            return failed();
    for (const auto &value : snapshot.value("entries").toArray()) {
        const QJsonArray row = value.toArray();
        if (!setEntry(row[0].toInt(), row[1].toInt(), {row[2].toInteger(), row[3].toInteger()}))
            return failed();
    }
    return commit();
}
