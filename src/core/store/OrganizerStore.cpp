#include "core/store/OrganizerStore.h"

#include "core/Logging.h"
#include "core/store/ProfileStore.h"
#include "core/sync/IrmcSync.h"

#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

namespace qpm::core::store {

namespace {

using codec::CalendarEvent;
using codec::CalendarTask;
using codec::Note;
using sync::IrmcDomain;

Error queryError(const QString &what, const QSqlQuery &query)
{
    return Error(ErrorCode::IoFailure,
                 QStringLiteral("%1: %2").arg(what, query.lastError().text()));
}

// SQLite stores a null QString as NULL; the text columns are NOT NULL.
QString text(const QString &value)
{
    return value.isNull() ? QString(QLatin1StringView("")) : value;
}

// Sorts like the time it stands for.
QString isoUtc(const QDateTime &time)
{
    return time.isValid() ? time.toUTC().toString(Qt::ISODateWithMs) : QString(QLatin1StringView(""));
}

QString isoList(const QList<QDateTime> &times)
{
    QStringList parts;
    for (const QDateTime &time : times)
        parts.append(isoUtc(time));
    return text(parts.join(u','));
}

QString dateText(const QDate &date)
{
    return date.isValid() ? date.toString(Qt::ISODate) : QString(QLatin1StringView(""));
}

Result<void> exec(QSqlQuery &query, const QString &statement)
{
    if (!query.exec(statement))
        return queryError(QStringLiteral("Cannot change the organizer tables"), query);
    return {};
}

Result<void> deleteRow(const QSqlDatabase &db, const QString &table, quint32 luid)
{
    QSqlQuery query(db);
    // The table name is one of three fixed names of this file, never input.
    query.prepare(QStringLiteral("DELETE FROM %1 WHERE luid = :luid").arg(table));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
    if (!query.exec())
        return queryError(QStringLiteral("Cannot delete an organizer entry"), query);
    return {};
}

Result<void> insertEvent(QSqlQuery &query, const sync::RawObject &object, const CalendarEvent &event)
{
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO events (luid, calendar_url, calendar_title, summary, location, "
        "description, start_utc, end_utc, all_day, rrule, exdates, alarms, class, transp, color, "
        "raw, hash) VALUES (:luid, :url, :title, :summary, :location, :description, :start, :end, "
        ":allDay, :rrule, :exdates, :alarms, :class, :transp, :color, :raw, :hash)"));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(object.luid));
    query.bindValue(QStringLiteral(":url"), text(event.calendarUrl));
    query.bindValue(QStringLiteral(":title"), text(event.calendarTitle));
    query.bindValue(QStringLiteral(":summary"), text(event.summary));
    query.bindValue(QStringLiteral(":location"), text(event.location));
    query.bindValue(QStringLiteral(":description"), text(event.description));
    query.bindValue(QStringLiteral(":start"), isoUtc(event.start));
    query.bindValue(QStringLiteral(":end"), isoUtc(event.end));
    query.bindValue(QStringLiteral(":allDay"), event.allDay ? 1 : 0);
    query.bindValue(QStringLiteral(":rrule"), text(event.rrule));
    query.bindValue(QStringLiteral(":exdates"), isoList(event.exdates));
    query.bindValue(QStringLiteral(":alarms"), isoList(event.alarms));
    query.bindValue(QStringLiteral(":class"), static_cast<int>(event.visibility));
    query.bindValue(QStringLiteral(":transp"), static_cast<int>(event.availability));
    query.bindValue(QStringLiteral(":color"),
                    event.color ? QVariant(static_cast<qint64>(*event.color)) : QVariant());
    query.bindValue(QStringLiteral(":raw"), object.data);
    query.bindValue(QStringLiteral(":hash"), object.hash);
    if (!query.exec())
        return queryError(QStringLiteral("Cannot store an event"), query);
    return {};
}

Result<void> insertTask(QSqlQuery &query, const sync::RawObject &object, const CalendarTask &task)
{
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO tasks (luid, summary, description, start_date, due_date, "
        "reminder_utc, rrule, priority, completed, categories, modified_utc, raw, hash) VALUES "
        "(:luid, :summary, :description, :start, :due, :reminder, :rrule, :priority, :completed, "
        ":categories, :modified, :raw, :hash)"));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(object.luid));
    query.bindValue(QStringLiteral(":summary"), text(task.summary));
    query.bindValue(QStringLiteral(":description"), text(task.description));
    query.bindValue(QStringLiteral(":start"), dateText(task.start));
    query.bindValue(QStringLiteral(":due"), dateText(task.due));
    query.bindValue(QStringLiteral(":reminder"), isoUtc(task.reminder));
    query.bindValue(QStringLiteral(":rrule"), text(task.rrule));
    query.bindValue(QStringLiteral(":priority"), static_cast<int>(task.priority));
    query.bindValue(QStringLiteral(":completed"), task.completed ? 1 : 0);
    query.bindValue(QStringLiteral(":categories"), text(task.categories.join(u'\n')));
    query.bindValue(QStringLiteral(":modified"), isoUtc(task.lastModified));
    query.bindValue(QStringLiteral(":raw"), object.data);
    query.bindValue(QStringLiteral(":hash"), object.hash);
    if (!query.exec())
        return queryError(QStringLiteral("Cannot store a task"), query);
    return {};
}

Result<void> insertNote(QSqlQuery &query, const sync::RawObject &object, const Note &note)
{
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO notes (luid, body, modified_utc, categories, raw, hash) VALUES "
        "(:luid, :body, :modified, :categories, :raw, :hash)"));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(object.luid));
    query.bindValue(QStringLiteral(":body"), text(note.body));
    query.bindValue(QStringLiteral(":modified"), isoUtc(note.lastModified));
    query.bindValue(QStringLiteral(":categories"), text(note.categories.join(u'\n')));
    query.bindValue(QStringLiteral(":raw"), object.data);
    query.bindValue(QStringLiteral(":hash"), object.hash);
    if (!query.exec())
        return queryError(QStringLiteral("Cannot store a note"), query);
    return {};
}

Result<void> writeCategories(const QSqlDatabase &db, const QString &domain,
                             const QList<codec::OrganizerCategory> &categories)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM categories WHERE domain = :domain"));
    query.bindValue(QStringLiteral(":domain"), domain);
    if (!query.exec())
        return queryError(QStringLiteral("Cannot clear the categories"), query);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO categories (domain, id, name, color) "
                                 "VALUES (:domain, :id, :name, :color)"));
    for (const codec::OrganizerCategory &category : categories) {
        query.bindValue(QStringLiteral(":domain"), domain);
        query.bindValue(QStringLiteral(":id"), category.id);
        query.bindValue(QStringLiteral(":name"), text(category.name));
        query.bindValue(QStringLiteral(":color"), static_cast<qint64>(category.color));
        if (!query.exec())
            return queryError(QStringLiteral("Cannot store a category"), query);
    }
    return {};
}

Result<void> writeMeta(const QSqlDatabase &db, const QString &key, const QString &value)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO meta (key, value) VALUES (:key, :value)"));
    query.bindValue(QStringLiteral(":key"), key);
    query.bindValue(QStringLiteral(":value"), value);
    if (!query.exec())
        return queryError(QStringLiteral("Cannot store a setting of the calendar"), query);
    return {};
}

Result<void> writeCalendarMetadata(const QSqlDatabase &db, const QByteArray &xml)
{
    const Result<codec::CalMetadata> metadata = codec::parseCalMetadata(xml);
    if (!metadata.hasValue()) {
        // The events stay usable without the list of calendars.
        qCWarning(lcStore) << "Calendar metadata not stored:" << metadata.error().message();
        return {};
    }
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("DELETE FROM calendars")))
        return queryError(QStringLiteral("Cannot clear the calendars"), query);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO calendars (url, title, color, hidden, timezone, access_level, "
        "selected, is_default) VALUES (:url, :title, :color, :hidden, :timezone, :access, "
        ":selected, :isDefault)"));
    for (const codec::CalendarInfo &calendar : metadata.value().calendars) {
        query.bindValue(QStringLiteral(":url"), text(calendar.url));
        query.bindValue(QStringLiteral(":title"), text(calendar.title));
        query.bindValue(QStringLiteral(":color"), static_cast<qint64>(calendar.color));
        query.bindValue(QStringLiteral(":hidden"), calendar.hidden ? 1 : 0);
        query.bindValue(QStringLiteral(":timezone"), text(calendar.timezone));
        query.bindValue(QStringLiteral(":access"), calendar.accessLevel);
        query.bindValue(QStringLiteral(":selected"), calendar.selected ? 1 : 0);
        query.bindValue(QStringLiteral(":isDefault"), calendar.isDefault ? 1 : 0);
        if (!query.exec())
            return queryError(QStringLiteral("Cannot store a calendar"), query);
    }
    if (const Result<void> stored =
            writeCategories(db, QStringLiteral("task"), metadata.value().taskCategories);
        !stored.hasValue()) {
        return stored;
    }
    if (const Result<void> stored =
            writeMeta(db, QStringLiteral("cal.filterDisabled"),
                      metadata.value().filterDisabled ? QStringLiteral("1") : QStringLiteral("0"));
        !stored.hasValue()) {
        return stored;
    }
    return writeMeta(db, QStringLiteral("cal.taskSupport"),
                     metadata.value().taskSupport ? QStringLiteral("1") : QStringLiteral("0"));
}

Result<void> writeNoteMetadata(const QSqlDatabase &db, const QByteArray &xml)
{
    const Result<QList<codec::OrganizerCategory>> categories = codec::parseNoteMetadata(xml);
    if (!categories.hasValue()) {
        // The notes stay usable without their categories.
        qCWarning(lcStore) << "Note metadata not stored:" << categories.error().message();
        return {};
    }
    return writeCategories(db, QStringLiteral("note"), categories.value());
}

Result<QSet<quint32>> storedLuids(const QSqlDatabase &db, IrmcDomain domain)
{
    QSqlQuery query(db);
    const QString statement = domain == IrmcDomain::Notes
                                  ? QStringLiteral("SELECT luid FROM notes")
                                  : QStringLiteral("SELECT luid FROM events UNION SELECT luid FROM tasks");
    if (!query.exec(statement))
        return queryError(QStringLiteral("Cannot read the organizer entries"), query);
    QSet<quint32> luids;
    while (query.next())
        luids.insert(query.value(0).toUInt());
    return luids;
}

Result<QList<CalendarEvent>> eventsFrom(QSqlQuery &query)
{
    QList<CalendarEvent> events;
    while (query.next()) {
        const quint32 luid = query.value(0).toUInt();
        const Result<CalendarEvent> parsed = codec::parsePhoneEvent(query.value(1).toByteArray());
        if (!parsed.hasValue()) {
            // The list goes on without the entry; its stored row stays as it is.
            qCWarning(lcStore) << "Appointment" << sync::IrmcSync::luidText(luid)
                               << "not readable:" << parsed.error().message();
            continue;
        }
        CalendarEvent event = parsed.value();
        event.luid = luid;
        events.append(event);
    }
    return events;
}

QList<codec::OrganizerCategory> categoriesFrom(QSqlQuery &query)
{
    QList<codec::OrganizerCategory> categories;
    while (query.next()) {
        categories.append({query.value(0).toInt(), query.value(1).toString(),
                           static_cast<quint32>(query.value(2).toLongLong())});
    }
    return categories;
}

} // namespace

OrganizerStore::OrganizerStore(ProfileStore *profile) : m_profile(profile)
{
    Q_ASSERT(profile);
}

std::optional<quint64> OrganizerStore::changeCounter(IrmcDomain domain) const
{
    return m_profile->changeCounter(domain);
}

std::optional<QByteArray> OrganizerStore::objectHash(IrmcDomain domain, quint32 luid) const
{
    return m_profile->objectHash(domain, luid);
}

Result<void> OrganizerStore::apply(IrmcDomain domain, const sync::SyncDelta &delta)
{
    if (domain != IrmcDomain::Calendar && domain != IrmcDomain::Notes)
        return m_profile->apply(domain, delta);

    const QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    const Result<QSet<quint32>> before = storedLuids(db, domain);
    if (!before.hasValue())
        return before.error();
    OrganizerChanges changes;
    QSet<quint32> upserted;
    for (const sync::RawObject &object : delta.upserts) {
        upserted.insert(object.luid);
        if (before.value().contains(object.luid))
            ++changes.changed;
        else
            ++changes.added;
    }
    if (delta.full) {
        for (const quint32 luid : before.value()) {
            if (!upserted.contains(luid))
                ++changes.removed;
        }
    } else {
        for (const quint32 luid : delta.deletes) {
            if (before.value().contains(luid))
                ++changes.removed;
        }
    }
    const Result<void> applied = applyOne(domain, delta);
    if (applied.hasValue())
        m_lastChanges = changes;
    return applied;
}

Result<void> OrganizerStore::applyOne(IrmcDomain domain, const sync::SyncDelta &delta)
{
    return m_profile->applyWith(domain, delta,
                                [this, domain, &delta](const QString &connectionName) {
                                    return writeRows(connectionName, domain, delta);
                                });
}

Result<void> OrganizerStore::writeRows(const QString &connectionName, IrmcDomain domain,
                                       const sync::SyncDelta &delta)
{
    const QSqlDatabase db = QSqlDatabase::database(connectionName, false);
    QSqlQuery query(db);
    const bool notes = domain == IrmcDomain::Notes;
    const QStringList tables = notes ? QStringList{QStringLiteral("notes")}
                                     : QStringList{QStringLiteral("events"), QStringLiteral("tasks")};
    if (delta.full) {
        for (const QString &table : tables) {
            // The table name is one of three fixed names of this file.
            if (const Result<void> cleared = exec(query, QStringLiteral("DELETE FROM %1").arg(table));
                !cleared.hasValue()) {
                return cleared;
            }
        }
    }
    QList<quint32> removed = delta.deletes;
    if (!delta.full) {
        for (const sync::RawObject &object : delta.upserts)
            removed.append(object.luid);
    }
    for (const quint32 luid : std::as_const(removed)) {
        for (const QString &table : tables) {
            if (const Result<void> gone = deleteRow(db, table, luid); !gone.hasValue())
                return gone;
        }
    }

    for (const sync::RawObject &object : delta.upserts) {
        Result<void> stored;
        if (notes) {
            const Result<Note> note = codec::parsePhoneNote(object.data);
            if (!note.hasValue()) {
                qCWarning(lcStore) << "Note" << sync::IrmcSync::luidText(object.luid)
                                   << "not readable:" << note.error().message();
                continue;
            }
            stored = insertNote(query, object, note.value());
        } else if (codec::isTaskObject(object.data)) {
            const Result<CalendarTask> task = codec::parsePhoneTask(object.data);
            if (!task.hasValue()) {
                qCWarning(lcStore) << "Task" << sync::IrmcSync::luidText(object.luid)
                                   << "not readable:" << task.error().message();
                continue;
            }
            stored = insertTask(query, object, task.value());
        } else {
            const Result<CalendarEvent> event = codec::parsePhoneEvent(object.data);
            if (!event.hasValue()) {
                // The object stays in the sync state; a later pull may bring
                // a readable version.
                qCWarning(lcStore) << "Event" << sync::IrmcSync::luidText(object.luid)
                                   << "not readable:" << event.error().message();
                continue;
            }
            stored = insertEvent(query, object, event.value());
        }
        if (!stored.hasValue())
            return stored;
    }

    if (delta.metadata.isEmpty())
        return {};
    return notes ? writeNoteMetadata(db, delta.metadata) : writeCalendarMetadata(db, delta.metadata);
}

Result<codec::CalMetadata> OrganizerStore::calendarMetadata() const
{
    const QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    QSqlQuery query(db);
    codec::CalMetadata metadata;
    if (!query.exec(QStringLiteral("SELECT url, title, color, hidden, timezone, access_level, "
                                   "selected, is_default FROM calendars ORDER BY title, url"))) {
        return queryError(QStringLiteral("Cannot read the calendars"), query);
    }
    while (query.next()) {
        codec::CalendarInfo calendar;
        calendar.url = query.value(0).toString();
        calendar.title = query.value(1).toString();
        calendar.color = static_cast<quint32>(query.value(2).toLongLong());
        calendar.hidden = query.value(3).toBool();
        calendar.timezone = query.value(4).toString();
        calendar.accessLevel = query.value(5).toInt();
        calendar.selected = query.value(6).toBool();
        calendar.isDefault = query.value(7).toBool();
        metadata.calendars.append(calendar);
    }
    if (!query.exec(QStringLiteral("SELECT id, name, color FROM categories WHERE domain = 'task' "
                                   "ORDER BY name, id"))) {
        return queryError(QStringLiteral("Cannot read the task categories"), query);
    }
    metadata.taskCategories = categoriesFrom(query);
    if (!query.exec(QStringLiteral("SELECT key, value FROM meta WHERE key IN "
                                   "('cal.filterDisabled', 'cal.taskSupport')"))) {
        return queryError(QStringLiteral("Cannot read the calendar settings"), query);
    }
    while (query.next()) {
        const bool on = query.value(1).toString() == QLatin1StringView("1");
        if (query.value(0).toString() == QLatin1StringView("cal.filterDisabled"))
            metadata.filterDisabled = on;
        else
            metadata.taskSupport = on;
    }
    return metadata;
}

Result<QList<codec::OrganizerCategory>> OrganizerStore::noteCategories() const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    if (!query.exec(QStringLiteral("SELECT id, name, color FROM categories WHERE domain = 'note' "
                                   "ORDER BY name, id"))) {
        return queryError(QStringLiteral("Cannot read the note categories"), query);
    }
    return categoriesFrom(query);
}

Result<QList<CalendarEvent>> OrganizerStore::eventsBetween(const QDateTime &from,
                                                           const QDateTime &to) const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    query.prepare(QStringLiteral(
        "SELECT luid, raw FROM events WHERE (rrule = '' AND start_utc < :to AND "
        "(end_utc > :from OR start_utc >= :from)) OR (rrule <> '' AND start_utc < :to) "
        "ORDER BY start_utc, luid"));
    query.bindValue(QStringLiteral(":from"), isoUtc(from));
    query.bindValue(QStringLiteral(":to"), isoUtc(to));
    if (!query.exec())
        return queryError(QStringLiteral("Cannot read the events"), query);
    return eventsFrom(query);
}

Result<QList<CalendarEvent>> OrganizerStore::allEvents() const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    if (!query.exec(QStringLiteral("SELECT luid, raw FROM events ORDER BY start_utc, luid")))
        return queryError(QStringLiteral("Cannot read the events"), query);
    return eventsFrom(query);
}

Result<CalendarEvent> OrganizerStore::event(quint32 luid) const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    query.prepare(QStringLiteral("SELECT luid, raw FROM events WHERE luid = :luid"));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
    if (!query.exec())
        return queryError(QStringLiteral("Cannot read an event"), query);
    const Result<QList<CalendarEvent>> found = eventsFrom(query);
    if (!found.hasValue())
        return found.error();
    if (found.value().isEmpty()) {
        return Error(ErrorCode::NotFound,
                     QStringLiteral("No event %1").arg(sync::IrmcSync::luidText(luid)));
    }
    return found.value().constFirst();
}

Result<QList<CalendarTask>> OrganizerStore::tasks() const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    if (!query.exec(QStringLiteral("SELECT luid, raw FROM tasks ORDER BY completed, "
                                   "due_date = '', due_date, luid"))) {
        return queryError(QStringLiteral("Cannot read the tasks"), query);
    }
    QList<CalendarTask> tasks;
    while (query.next()) {
        const quint32 luid = query.value(0).toUInt();
        const Result<CalendarTask> parsed = codec::parsePhoneTask(query.value(1).toByteArray());
        if (!parsed.hasValue()) {
            // The list goes on without the entry; its stored row stays as it is.
            qCWarning(lcStore) << "Task" << sync::IrmcSync::luidText(luid)
                               << "not readable:" << parsed.error().message();
            continue;
        }
        CalendarTask task = parsed.value();
        task.luid = luid;
        tasks.append(task);
    }
    return tasks;
}

Result<CalendarTask> OrganizerStore::task(quint32 luid) const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    query.prepare(QStringLiteral("SELECT raw FROM tasks WHERE luid = :luid"));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
    if (!query.exec())
        return queryError(QStringLiteral("Cannot read a task"), query);
    if (!query.next()) {
        return Error(ErrorCode::NotFound,
                     QStringLiteral("No task %1").arg(sync::IrmcSync::luidText(luid)));
    }
    Result<CalendarTask> parsed = codec::parsePhoneTask(query.value(0).toByteArray());
    if (!parsed.hasValue())
        return parsed;
    CalendarTask task = parsed.value();
    task.luid = luid;
    return task;
}

Result<QList<Note>> OrganizerStore::notes() const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    if (!query.exec(QStringLiteral("SELECT luid, raw FROM notes ORDER BY modified_utc DESC, luid")))
        return queryError(QStringLiteral("Cannot read the notes"), query);
    QList<Note> notes;
    while (query.next()) {
        const quint32 luid = query.value(0).toUInt();
        const Result<Note> parsed = codec::parsePhoneNote(query.value(1).toByteArray());
        if (!parsed.hasValue()) {
            // The list goes on without the entry; its stored row stays as it is.
            qCWarning(lcStore) << "Note" << sync::IrmcSync::luidText(luid)
                               << "not readable:" << parsed.error().message();
            continue;
        }
        Note note = parsed.value();
        note.luid = luid;
        notes.append(note);
    }
    return notes;
}

Result<Note> OrganizerStore::note(quint32 luid) const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    query.prepare(QStringLiteral("SELECT raw FROM notes WHERE luid = :luid"));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
    if (!query.exec())
        return queryError(QStringLiteral("Cannot read a note"), query);
    if (!query.next()) {
        return Error(ErrorCode::NotFound,
                     QStringLiteral("No note %1").arg(sync::IrmcSync::luidText(luid)));
    }
    Result<Note> parsed = codec::parsePhoneNote(query.value(0).toByteArray());
    if (!parsed.hasValue())
        return parsed;
    Note note = parsed.value();
    note.luid = luid;
    return note;
}

Result<void> OrganizerStore::storeObject(IrmcDomain domain, const sync::RawObject &object)
{
    const std::optional<quint64> counter = changeCounter(domain);
    if (!counter) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("The area was never pulled, so nothing can be stored"));
    }
    sync::SyncDelta delta;
    delta.upserts.append(object);
    delta.newCounter = *counter;
    return applyOne(domain, delta);
}

Result<void> OrganizerStore::removeObject(IrmcDomain domain, quint32 luid)
{
    const std::optional<quint64> counter = changeCounter(domain);
    if (!counter) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("The area was never pulled, so nothing can be removed"));
    }
    sync::SyncDelta delta;
    delta.deletes.append(luid);
    delta.newCounter = *counter;
    return applyOne(domain, delta);
}

Result<void> OrganizerStore::storeMetadata(IrmcDomain domain, const QByteArray &xml)
{
    const std::optional<quint64> counter = changeCounter(domain);
    if (!counter) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("The area was never pulled, so nothing can be stored"));
    }
    sync::SyncDelta delta;
    delta.newCounter = *counter;
    delta.metadata = xml;
    return applyOne(domain, delta);
}

} // namespace qpm::core::store
