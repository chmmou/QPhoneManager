#include "core/store/CallStore.h"

#include "core/store/ProfileStore.h"

#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace qpm::core::store {

namespace {

using codec::CallDirection;
using codec::CallEntry;

QString directionText(CallDirection direction)
{
    switch (direction) {
    case CallDirection::Incoming:
        return QStringLiteral("incoming");
    case CallDirection::Missed:
        return QStringLiteral("missed");
    case CallDirection::Outgoing:
        break;
    }
    return QStringLiteral("outgoing");
}

CallDirection directionFromText(const QString &text)
{
    if (text == QLatin1StringView("incoming"))
        return CallDirection::Incoming;
    if (text == QLatin1StringView("missed"))
        return CallDirection::Missed;
    return CallDirection::Outgoing;
}

// SQLite stores a null QString as NULL; the text columns are NOT NULL.
QString text(const QString &value)
{
    return value.isNull() ? QString(QLatin1StringView("")) : value;
}

Error queryError(const QString &what, const QSqlQuery &query)
{
    return Error(ErrorCode::IoFailure,
                 QStringLiteral("%1: %2").arg(what, query.lastError().text()));
}

// Compared by the instant, whatever zone the two times carry.
bool sameEntry(const CallEntry &a, const CallEntry &b)
{
    return a.id == b.id && a.number == b.number && a.name == b.name && a.direction == b.direction &&
           a.start.toSecsSinceEpoch() == b.start.toSecsSinceEpoch() &&
           a.durationSeconds == b.durationSeconds && a.simSlot == b.simSlot;
}

} // namespace

CallStore::CallStore(ProfileStore *profile) : m_profile(profile)
{
    Q_ASSERT(profile);
}

Result<CallChanges> CallStore::replaceAll(const QList<CallEntry> &entries)
{
    const Result<QList<CallEntry>> stored = calls();
    if (!stored.hasValue())
        return stored.error();
    QHash<quint32, CallEntry> before;
    for (const CallEntry &entry : stored.value())
        before.insert(entry.id, entry);

    CallChanges changes;
    QSet<quint32> kept;
    for (const CallEntry &entry : entries) {
        if (kept.contains(entry.id))
            continue;
        kept.insert(entry.id);
        const auto old = before.constFind(entry.id);
        if (old == before.cend())
            ++changes.added;
        else if (!sameEntry(old.value(), entry))
            ++changes.changed;
    }
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        if (!kept.contains(it.key()))
            ++changes.removed;
    }

    QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    if (!db.transaction())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot start a transaction"));
    const auto fail = [&db](const Error &error) -> Result<CallChanges> {
        db.rollback();
        return error;
    };

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("DELETE FROM calls")))
        return fail(queryError(QStringLiteral("Cannot clear the call log"), query));
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO calls (id, number, name, direction, start_utc, duration, sim) "
        "VALUES (:id, :number, :name, :direction, :start, :duration, :sim)"));
    for (const CallEntry &entry : entries) {
        query.bindValue(QStringLiteral(":id"), static_cast<qint64>(entry.id));
        query.bindValue(QStringLiteral(":number"), text(entry.number));
        query.bindValue(QStringLiteral(":name"), text(entry.name));
        query.bindValue(QStringLiteral(":direction"), directionText(entry.direction));
        query.bindValue(QStringLiteral(":start"), entry.start.toUTC().toString(Qt::ISODate));
        query.bindValue(QStringLiteral(":duration"), entry.durationSeconds);
        query.bindValue(QStringLiteral(":sim"), entry.simSlot);
        if (!query.exec())
            return fail(queryError(QStringLiteral("Cannot store a call"), query));
    }
    if (!db.commit())
        return fail(Error(ErrorCode::IoFailure, QStringLiteral("Cannot commit the call log")));
    return changes;
}

Result<QList<CallEntry>> CallStore::calls() const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    if (!query.exec(QStringLiteral("SELECT id, number, name, direction, start_utc, duration, sim "
                                   "FROM calls ORDER BY start_utc DESC, id DESC"))) {
        return queryError(QStringLiteral("Cannot read the call log"), query);
    }
    QList<CallEntry> entries;
    while (query.next()) {
        CallEntry entry;
        entry.id = query.value(0).toUInt();
        entry.number = query.value(1).toString();
        entry.name = query.value(2).toString();
        entry.direction = directionFromText(query.value(3).toString());
        entry.start = QDateTime::fromString(query.value(4).toString(), Qt::ISODate).toLocalTime();
        entry.durationSeconds = query.value(5).toLongLong();
        entry.simSlot = query.value(6).toInt();
        entries.append(entry);
    }
    return entries;
}

Result<void> CallStore::remove(const QList<quint32> &ids)
{
    if (ids.isEmpty())
        return {};
    QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    if (!db.transaction())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot start a transaction"));
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM calls WHERE id = :id"));
    for (const quint32 id : ids) {
        query.bindValue(QStringLiteral(":id"), static_cast<qint64>(id));
        if (!query.exec()) {
            const Error error = queryError(QStringLiteral("Cannot delete a call"), query);
            db.rollback();
            return error;
        }
    }
    if (!db.commit())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot commit the call log"));
    return {};
}

} // namespace qpm::core::store
