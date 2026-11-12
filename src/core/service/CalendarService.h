#pragma once

#include "core/Error.h"
#include "core/codec/CalMetadata.h"
#include "core/codec/PhoneVCal.h"
#include "core/service/SaveMode.h"
#include "core/store/OrganizerStore.h"
#include "core/sync/SyncStoreAdapter.h"

#include <QFuture>
#include <QList>
#include <QObject>

#include <memory>

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::service {

/// The result of saving an event.
struct EventSaveOutcome
{
    /// LUID of the saved event; 0 when a conflict stopped the save.
    quint32 luid = 0;
    /// The event changed on the phone; nothing was written.
    bool conflict = false;
    /// The phone's version when `conflict` is set.
    codec::CalendarEvent phoneVersion;
    /// The phone's version as it came from the phone, for keepPhoneVersion().
    sync::RawObject phoneObject;
};

/// The result of saving a task.
struct TaskSaveOutcome
{
    /// LUID of the saved task; 0 when a conflict stopped the save.
    quint32 luid = 0;
    /// The task changed on the phone; nothing was written.
    bool conflict = false;
    /// The phone's version when `conflict` is set.
    codec::CalendarTask phoneVersion;
    /// The phone's version as it came from the phone, for keepPhoneVersion().
    sync::RawObject phoneObject;
};

/// The result of an import into the phone.
struct OrganizerImportOutcome
{
    int created = 0;
    /// Entries the phone did not take.
    int skipped = 0;
};

/**
 * The calendar and the tasks of the connected phone: pulling them into the
 * organizer store, creating, changing and deleting events and tasks, the
 * task categories, the choice of synced calendars and imports. Changes go to
 * the phone first; the store then takes the phone's version.
 *
 * Events are only created in a calendar the phone may write to. Before an
 * event goes into a calendar the phone does not sync yet, that calendar is
 * added to the synced ones in a session of its own, since the phone reads
 * that choice only when a session starts.
 *
 * Every call that needs the phone fails with ErrorCode::ConnectionClosed
 * when it is not connected, and every call that needs the store with
 * ErrorCode::InvalidInput when none is set. Lives in the main thread; no call
 * blocks. Does not own the connection, which must outlive it.
 */
class CalendarService : public QObject
{
    Q_OBJECT

public:
    explicit CalendarService(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /**
     * The store of the current phone; null when none is chosen. Held weakly:
     * a call that finishes after the store is gone fails with
     * ErrorCode::Cancelled and changes nothing.
     */
    void setStore(const std::shared_ptr<store::OrganizerStore> &store);

    /// Pulls what changed in the calendar and the tasks; everything on the first pull.
    QFuture<Result<store::OrganizerChanges>> sync();

    /**
     * Creates `edited` in its calendar when its LUID is 0, otherwise replaces
     * it on the phone. With SaveMode::CheckConflict, an event that differs on
     * the phone from the stored one is not written; the outcome carries the
     * phone's version. Fails with ErrorCode::InvalidInput for an event without
     * a start or without a title, and for a new event in a calendar that is
     * unknown or not writable; with ErrorCode::NotFound when the event was
     * deleted on the phone.
     */
    QFuture<Result<EventSaveOutcome>> saveEvent(const codec::CalendarEvent &edited, SaveMode mode);

    /// Like saveEvent() for a task, which needs a title.
    QFuture<Result<TaskSaveOutcome>> saveTask(const codec::CalendarTask &edited, SaveMode mode);

    /// Stores the phone's version after a conflict, dropping the local edit.
    [[nodiscard]] Result<void> keepPhoneVersion(const sync::RawObject &object);

    /// Deletes events or tasks on the phone; one already gone counts as deleted.
    QFuture<Result<void>> remove(const QList<quint32> &luids);

    /// Adds, renames, recolours or deletes task categories, then stores what the phone keeps.
    QFuture<Result<void>> changeTaskCategories(const QList<codec::CategoryChange> &changes);

    /**
     * Makes the calendars with `selected` the synced ones and the one with
     * `isDefault` the one for new events, then pulls the calendar. Calendars
     * not in the list stay out of the sync.
     */
    QFuture<Result<void>> setCalendarSelection(const QList<codec::CalendarInfo> &calendars);

    /**
     * Creates the events in `target` and the tasks on the phone, then pulls
     * the calendar. An entry the phone does not take is skipped and counted.
     * Fails with ErrorCode::InvalidInput when there are events and `target`
     * is not writable.
     */
    QFuture<Result<OrganizerImportOutcome>> importItems(const QList<codec::CalendarEvent> &events,
                                                        const QList<codec::CalendarTask> &tasks,
                                                        const codec::CalendarInfo &target);

Q_SIGNALS:
    /// The stored events, tasks, calendars or task categories changed.
    void calendarChanged();

private:
    QFuture<Result<EventSaveOutcome>> createEvent(const codec::CalendarEvent &edited,
                                                  const codec::CalendarInfo &target);

    protocol::PhoneConnection *m_connection = nullptr;
    std::weak_ptr<store::OrganizerStore> m_store;
};

} // namespace qpm::core::service
