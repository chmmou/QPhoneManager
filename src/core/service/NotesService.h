#pragma once

#include "core/Error.h"
#include "core/codec/CalMetadata.h"
#include "core/codec/VNote.h"
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

/// The result of saving a note.
struct NoteSaveOutcome
{
    /// LUID of the saved note; 0 when a conflict stopped the save.
    quint32 luid = 0;
    /// The note changed on the phone; nothing was written.
    bool conflict = false;
    /// The phone's version when `conflict` is set.
    codec::Note phoneVersion;
    /// The phone's version as it came from the phone, for keepPhoneVersion().
    sync::RawObject phoneObject;
};

/**
 * The notes of the connected phone: pulling them into the organizer store,
 * creating, changing and deleting them and their categories. Changes go to
 * the phone first; the store then takes the phone's version.
 *
 * Every call that needs the phone fails with ErrorCode::ConnectionClosed
 * when it is not connected, and every call that needs the store with
 * ErrorCode::InvalidInput when none is set. Lives in the main thread; no call
 * blocks. Does not own the connection, which must outlive it.
 */
class NotesService : public QObject
{
    Q_OBJECT

public:
    explicit NotesService(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /// The store of the current phone, held weakly like CalendarService::setStore().
    void setStore(const std::shared_ptr<store::OrganizerStore> &store);

    /// Pulls what changed in the notes; everything on the first pull.
    QFuture<Result<store::OrganizerChanges>> sync();

    /**
     * Creates `edited` when its LUID is 0, otherwise replaces it on the phone.
     * With SaveMode::CheckConflict, a note that differs on the phone from the
     * stored one is not written; the outcome carries the phone's version.
     * Fails with ErrorCode::InvalidInput for a note without text and with
     * ErrorCode::NotFound when the note was deleted on the phone.
     */
    QFuture<Result<NoteSaveOutcome>> save(const codec::Note &edited, SaveMode mode);

    /// Stores the phone's version after a conflict, dropping the local edit.
    [[nodiscard]] Result<void> keepPhoneVersion(const sync::RawObject &object);

    /// Deletes notes on the phone; one already gone counts as deleted.
    QFuture<Result<void>> remove(const QList<quint32> &luids);

    /// Adds, renames, recolours or deletes note categories, then stores what the phone keeps.
    QFuture<Result<void>> changeCategories(const QList<codec::CategoryChange> &changes);

Q_SIGNALS:
    /// The stored notes or note categories changed.
    void notesChanged();

private:
    protocol::PhoneConnection *m_connection = nullptr;
    std::weak_ptr<store::OrganizerStore> m_store;
};

} // namespace qpm::core::service
