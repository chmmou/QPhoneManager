#pragma once

#include "core/Error.h"
#include "core/codec/CalMetadata.h"
#include "core/codec/PhoneVCal.h"
#include "core/codec/VNote.h"
#include "core/sync/SyncStoreAdapter.h"

#include <QDateTime>
#include <QList>

#include <optional>

namespace qpm::core::store {

class ProfileStore;

/// What the last apply() of an area changed.
struct OrganizerChanges
{
    int added = 0;
    int changed = 0;
    int removed = 0;
};

/**
 * The calendar, the tasks and the notes of one phone on this computer: the
 * tables calendars, events, tasks, notes and categories of the profile
 * database, filled from every pull of "telecom/cal" and "telecom/notes".
 * Each row keeps the text the phone sent; reading parses that text, so the
 * columns beside it serve the search by time.
 *
 * As a SyncStoreAdapter it stands in front of the profile store: the sync
 * state of every area stays there, and a pull of the calendar or the notes
 * also rewrites these tables in the same transaction. An object that cannot
 * be read is kept in the sync state, logged and left out of the tables.
 *
 * The profile store must be open and must outlive this object. Use from the
 * main thread only.
 */
class OrganizerStore : public sync::SyncStoreAdapter
{
public:
    explicit OrganizerStore(ProfileStore *profile);

    [[nodiscard]] std::optional<quint64> changeCounter(sync::IrmcDomain domain) const override;
    [[nodiscard]] std::optional<QByteArray> objectHash(sync::IrmcDomain domain,
                                                       quint32 luid) const override;
    /// For the calendar and the notes also stores the metadata, when the delta carries it.
    [[nodiscard]] Result<void> apply(sync::IrmcDomain domain,
                                     const sync::SyncDelta &delta) override;

    /// What the last successful apply() of the calendar or the notes changed.
    [[nodiscard]] OrganizerChanges lastChanges() const { return m_lastChanges; }

    /// The calendars and task categories from the last pull; empty before the first.
    [[nodiscard]] Result<codec::CalMetadata> calendarMetadata() const;
    /// The note categories from the last pull, by name.
    [[nodiscard]] Result<QList<codec::OrganizerCategory>> noteCategories() const;

    /**
     * Events that may take place between `from` and `to`: single events that
     * overlap the span, and every series that starts before `to`, which the
     * caller expands. Sorted by start.
     */
    [[nodiscard]] Result<QList<codec::CalendarEvent>> eventsBetween(const QDateTime &from,
                                                                    const QDateTime &to) const;
    /// Every stored event, sorted by start.
    [[nodiscard]] Result<QList<codec::CalendarEvent>> allEvents() const;
    /// Fails with ErrorCode::NotFound for an unknown LUID.
    [[nodiscard]] Result<codec::CalendarEvent> event(quint32 luid) const;

    /// Every stored task, open ones first, then by due date.
    [[nodiscard]] Result<QList<codec::CalendarTask>> tasks() const;
    /// Fails with ErrorCode::NotFound for an unknown LUID.
    [[nodiscard]] Result<codec::CalendarTask> task(quint32 luid) const;

    /// Every stored note, newest change first.
    [[nodiscard]] Result<QList<codec::Note>> notes() const;
    /// Fails with ErrorCode::NotFound for an unknown LUID.
    [[nodiscard]] Result<codec::Note> note(quint32 luid) const;

    /**
     * Stores one object of the calendar or the notes, fetched after this
     * program wrote it, keeping the change counter. Requires an earlier pull
     * of that area.
     */
    [[nodiscard]] Result<void> storeObject(sync::IrmcDomain domain, const sync::RawObject &object);
    /// Removes one object after it was deleted on the phone.
    [[nodiscard]] Result<void> removeObject(sync::IrmcDomain domain, quint32 luid);
    /// Replaces the stored metadata of the calendar or the notes with `xml`.
    [[nodiscard]] Result<void> storeMetadata(sync::IrmcDomain domain, const QByteArray &xml);

private:
    [[nodiscard]] Result<void> writeRows(const QString &connectionName, sync::IrmcDomain domain,
                                         const sync::SyncDelta &delta);
    [[nodiscard]] Result<void> applyOne(sync::IrmcDomain domain, const sync::SyncDelta &delta);

    ProfileStore *m_profile = nullptr;
    OrganizerChanges m_lastChanges;
};

} // namespace qpm::core::store
