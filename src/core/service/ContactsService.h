#pragma once

#include "core/Error.h"
#include "core/codec/PhoneVCard.h"
#include "core/store/ContactStore.h"
#include "core/sync/SyncStoreAdapter.h"

#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

namespace qpm::core::protocol {
class ObexClient;
class PhoneConnection;
} // namespace qpm::core::protocol

namespace qpm::core::service {

/// Two contacts that look like the same person.
struct DuplicatePair
{
    enum class Reason { Number, Email, Name };

    quint32 first = 0;
    quint32 second = 0;
    Reason reason = Reason::Name;

    bool operator==(const DuplicatePair &other) const = default;
};

/// How a save treats a contact that changed on the phone since it was loaded.
enum class SaveMode {
    /// Stop and report both versions.
    CheckConflict,
    /// Write the edited version over the phone's.
    Overwrite,
};

/// The result of a save.
struct SaveOutcome
{
    /// LUID of the saved contact; 0 when a conflict stopped the save.
    quint32 luid = 0;
    /// The contact changed on the phone; nothing was written.
    bool conflict = false;
    /// The phone's version when `conflict` is set.
    codec::Contact phoneVersion;
    /// The phone's version as it came from the phone, for keepPhoneVersion().
    sync::RawObject phoneObject;
};

/// The result of an import.
struct ImportOutcome
{
    int created = 0;
    /// Contacts the phone cannot take, for example with a photo that is too large.
    int skipped = 0;
};

/**
 * The contacts of the connected phone: pulling them into the contact store,
 * creating, changing and deleting them on the phone, importing, merging and
 * dialling a number. Changes always go to the phone first; the store then
 * takes the phone's version, so it never holds what the phone does not.
 *
 * Every call that needs the phone fails with ErrorCode::ConnectionClosed when
 * it is not connected, and every call that needs the store with
 * ErrorCode::InvalidInput when none is set. Lives in the main thread; no call
 * blocks. Does not own the connection, which must outlive it.
 */
class ContactsService : public QObject
{
    Q_OBJECT

public:
    explicit ContactsService(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /**
     * The store of the current phone; null when none is chosen. The service
     * keeps only a weak reference: a call that finishes after the store is
     * gone fails with ErrorCode::Cancelled and changes nothing.
     */
    void setStore(const std::shared_ptr<store::ContactStore> &store);

    /// Pulls what changed on the phone; everything on the first pull.
    QFuture<Result<store::ContactChanges>> sync();

    /**
     * Creates `edited` when its LUID is 0, otherwise changes it on the phone.
     * Only entries that differ from the phone's version are sent. Without
     * `photoChanged` the photo stays as it is on the phone. With
     * SaveMode::CheckConflict, a contact that differs on the phone from the
     * stored one is not written; the outcome carries the phone's version.
     * Fails with ErrorCode::InvalidInput for a contact without name, number,
     * mail address or company, ErrorCode::LimitExceeded when it does not fit
     * into what the phone takes, and ErrorCode::NotFound when the contact was
     * deleted on the phone.
     */
    QFuture<Result<SaveOutcome>> save(const codec::Contact &edited, bool photoChanged,
                                      SaveMode mode);

    /// Stores the phone's version after a conflict, dropping the local edit.
    [[nodiscard]] Result<void> keepPhoneVersion(const sync::RawObject &object);

    /// Deletes the contacts on the phone; a contact already gone counts as deleted.
    QFuture<Result<void>> remove(const QList<quint32> &luids);

    /**
     * Creates every contact on the phone in the default account, then pulls
     * the phonebook. A contact the phone cannot take is skipped and counted.
     */
    QFuture<Result<ImportOutcome>> importContacts(const QList<codec::Contact> &contacts);

    /**
     * Joins the contact `drop` into `keep` on the phone, see merged(), and
     * deletes `drop`. Returns the LUID of the joined contact.
     */
    QFuture<Result<quint32>> merge(quint32 keep, quint32 drop);

    /**
     * Makes the account the one that gets new contacts and adds it to the
     * synced accounts, then pulls the phonebook. Fails with
     * ErrorCode::NotFound for an account the phone did not report.
     */
    QFuture<Result<void>> setDefaultAccount(const QString &type, const QString &name);

    /**
     * Asks the phone to call `number`. Only digits, "+", "*" and "#" are
     * allowed, 3 to 32 of them after removing spaces, dashes, slashes and
     * brackets; anything else fails with ErrorCode::InvalidInput without
     * reaching the phone. The phone's refusal is ErrorCode::Rejected.
     */
    QFuture<Result<void>> dial(const QString &number);

    /// The number reduced to what dial() sends, or empty when it is not a number.
    [[nodiscard]] static QString dialableNumber(const QString &number);

    /**
     * Pairs of contacts with the same number, the same mail address or a very
     * similar name. Numbers are compared on their last nine digits, so a
     * leading country code or zero does not matter. Each pair is listed once,
     * with the first reason found.
     */
    [[nodiscard]] static QList<DuplicatePair> findDuplicates(const QList<codec::Contact> &contacts);

    /**
     * `keep` with everything of `drop` it lacks: empty name parts and fields
     * filled, entries and groups added unless the same value is there, the
     * photo taken over when `keep` has none and it is small enough to send.
     * The entries taken over count as new for the phone.
     */
    [[nodiscard]] static codec::Contact merged(const codec::Contact &keep,
                                               const codec::Contact &drop);

Q_SIGNALS:
    /// The stored contacts changed.
    void contactsChanged();

private:
    QFuture<Result<store::ContactChanges>> pullInSession(protocol::ObexClient &obex);

    protocol::PhoneConnection *m_connection = nullptr;
    std::weak_ptr<store::ContactStore> m_store;
};

} // namespace qpm::core::service
