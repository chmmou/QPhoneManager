#pragma once

#include "core/Error.h"
#include "core/codec/PbMetadata.h"
#include "core/codec/PhoneVCard.h"
#include "core/sync/SyncStoreAdapter.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace qpm::core::store {

class ProfileStore;

/// What a contact list shows of one contact.
struct ContactSummary
{
    quint32 luid = 0;
    QString displayName;
    QString sortKey;
    QString primaryNumber;
    QString primaryEmail;
    QString organization;
    QString accountType;
    QString accountName;
    QStringList groups;
    /// Name of the photo blob of the profile store; empty without a photo.
    QString photoBlob;

    bool operator==(const ContactSummary &other) const = default;
};

/// What the last apply() changed in the contacts.
struct ContactChanges
{
    int added = 0;
    int changed = 0;
    int removed = 0;
};

/**
 * The contacts of one phone on this computer: the tables contacts,
 * contact_values, accounts and groups of the profile database, filled from
 * every pull of the phonebook. Photos go to the blobs of the profile.
 *
 * As a SyncStoreAdapter it stands in front of the profile store: the sync
 * state of every area stays there, and a pull of the phonebook also rewrites
 * these tables in the same transaction. A vCard that cannot be read is kept
 * in the sync state, logged and left out of the tables.
 *
 * The profile store must be open and must outlive this object. Use from the
 * main thread only.
 */
class ContactStore : public sync::SyncStoreAdapter
{
public:
    explicit ContactStore(ProfileStore *profile);

    [[nodiscard]] std::optional<quint64> changeCounter(sync::IrmcDomain domain) const override;
    [[nodiscard]] std::optional<QByteArray> objectHash(sync::IrmcDomain domain,
                                                       quint32 luid) const override;
    /// For the phonebook also stores the metadata, when the delta carries it.
    [[nodiscard]] Result<void> apply(sync::IrmcDomain domain,
                                     const sync::SyncDelta &delta) override;

    /// What the last successful apply() of the phonebook changed.
    [[nodiscard]] ContactChanges lastChanges() const { return m_lastChanges; }

    /// Every stored contact, sorted by name.
    [[nodiscard]] Result<QList<ContactSummary>> summaries() const;

    /// The stored contact. Fails with ErrorCode::NotFound for an unknown LUID.
    [[nodiscard]] Result<codec::Contact> contact(quint32 luid) const;

    /// Every stored contact in full, for duplicates and export.
    [[nodiscard]] Result<QList<codec::Contact>> allContacts() const;

    /**
     * LUIDs of the contacts whose name, number, mail address or company
     * contains `text`, without regard to case. Digits in `text` also match
     * numbers written with spaces or dashes.
     */
    [[nodiscard]] Result<QSet<quint32>> search(const QString &text) const;

    /// Accounts and groups from the last pull; empty before the first.
    [[nodiscard]] Result<codec::PbMetadata> metadata() const;

    /**
     * Stores one object fetched after this program wrote it, keeping the
     * change counter. Requires an earlier pull of the phonebook.
     */
    [[nodiscard]] Result<void> storeObject(const sync::RawObject &object);

    /// Removes one contact after it was deleted on the phone.
    [[nodiscard]] Result<void> removeObject(quint32 luid);

private:
    [[nodiscard]] Result<void> writeRows(const QString &connectionName,
                                         const sync::SyncDelta &delta);

    ProfileStore *m_profile = nullptr;
    ContactChanges m_lastChanges;
};

} // namespace qpm::core::store
