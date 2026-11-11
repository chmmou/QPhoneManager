#pragma once

#include "core/Error.h"
#include "core/codec/VMessage.h"
#include "core/sync/SyncStoreAdapter.h"

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace qpm::core::store {

class ProfileStore;

/// Messages kept only on this computer get LUIDs from here on; the phone never uses them.
inline constexpr quint32 localMessageLuidBase = 0x40000000;

/// What a pull of the messages changed on this computer.
struct MessageChanges
{
    int added = 0;
    int changed = 0;
    int removed = 0;
    /// New received messages that are not read yet, for a notice; empty after a full load.
    QList<quint32> arrived;
    /// Messages the phone sent that could not be read; they are left out.
    int unreadable = 0;
};

/// One stored part of a multimedia message.
struct StoredAttachment
{
    int position = 0;
    QString name;
    QString mimeType;
    qint64 size = 0;
    /// Name of the blob of the profile store; empty when the content stayed on the phone.
    QString blob;

    bool operator==(const StoredAttachment &other) const = default;
};

/// A message as this computer keeps it.
struct StoredMessage
{
    quint32 luid = 0;
    codec::MessageKind kind = codec::MessageKind::Sms;
    codec::MessageBox box = codec::MessageBox::Inbox;
    bool incoming = true;
    bool read = true;
    /// UTC.
    QDateTime date;
    QStringList addresses;
    QString subject;
    QString body;
    int simSlot = -1;
    /// Kept on this computer even when the phone deletes it.
    bool archived = false;
    QString threadKey;
    QList<StoredAttachment> attachments;
    /// Scheduled messages only.
    QList<codec::MessageRecipient> recipients;

    /// The phone still has this message.
    [[nodiscard]] bool isOnPhone() const { return luid > 0 && luid < localMessageLuidBase; }
};

/// One conversation: every message with the same other side.
struct ConversationSummary
{
    QString threadKey;
    /// The other side as the newest message names it.
    QString address;
    /// UTC.
    QDateTime lastDate;
    /// The start of the newest message, or its subject.
    QString snippet;
    /// The newest message has parts other than text.
    bool lastHasAttachments = false;
    int unread = 0;
    int count = 0;
    int archived = 0;
};

/// Messages read from the phone, made ready on a worker thread for applyPrepared().
struct PreparedMessages
{
    /// `messages` holds everything the phone has; every other message is gone there.
    bool full = false;
    QList<StoredMessage> messages;
    QList<quint32> deletes;
    int unreadable = 0;
};

/**
 * The messages of one phone on this computer: the tables messages and
 * attachments of the profile database, and messages_fts for the search where
 * SQLite offers full-text search. Parts of multimedia messages go to the
 * blobs of the profile.
 *
 * As a SyncStoreAdapter it stands in front of the profile store: a pull keeps
 * the sync state there and marks the message tables as behind until
 * prepare() and applyPrepared() have brought them up to date. While they are
 * behind, the next pull loads everything. Reading and writing the vMessages
 * and their parts happens in prepare(), off the main thread.
 *
 * An archived message stays when the phone deletes it; it then gets a LUID
 * from localMessageLuidBase on, like a message read from an archive file.
 *
 * The profile store must be open and must outlive this object. Use from the
 * main thread only, except prepare().
 */
class MessageStore : public sync::SyncStoreAdapter
{
public:
    explicit MessageStore(ProfileStore *profile);

    /// Nothing for messages while the message tables are behind the sync state.
    [[nodiscard]] std::optional<quint64> changeCounter(sync::IrmcDomain domain) const override;
    [[nodiscard]] std::optional<QByteArray> objectHash(sync::IrmcDomain domain,
                                                       quint32 luid) const override;
    [[nodiscard]] Result<void> apply(sync::IrmcDomain domain,
                                     const sync::SyncDelta &delta) override;

    /// The profile folder, for prepare().
    [[nodiscard]] QString profileDirectory() const;

    /**
     * Reads the vMessages of `delta` and writes their parts to the blobs below
     * `profileDirectory`. A vMessage that cannot be read is left out and
     * counted. Fails with ErrorCode::IoFailure when a part cannot be written.
     * Touches no database, so it runs on any thread.
     */
    [[nodiscard]] static Result<PreparedMessages> prepare(const sync::SyncDelta &delta,
                                                          const QString &profileDirectory);

    /**
     * Brings the message tables to what `prepared` holds in one transaction
     * and returns what changed. A message the phone deleted stays when it is
     * archived. Parts no message uses any more are deleted afterwards.
     */
    [[nodiscard]] Result<MessageChanges> applyPrepared(const PreparedMessages &prepared);

    /// Every conversation, newest first; scheduled messages are not part of one.
    [[nodiscard]] Result<QList<ConversationSummary>> conversations() const;

    /// The messages of one conversation, oldest first, with their parts.
    [[nodiscard]] Result<QList<StoredMessage>> conversation(const QString &threadKey) const;

    /// The scheduled messages with their recipients, earliest first.
    [[nodiscard]] Result<QList<StoredMessage>> scheduledMessages() const;

    /// Every message except the scheduled ones, by conversation and time, for an export.
    [[nodiscard]] Result<QList<StoredMessage>> allMessages() const;

    /// One message. Fails with ErrorCode::NotFound for an unknown LUID.
    [[nodiscard]] Result<StoredMessage> message(quint32 luid) const;

    /**
     * The conversations with a message whose text, subject or address holds
     * every word of `text`, without regard to case and accents. A word matches
     * the start of a word in the message where full-text search is available,
     * and any part of the text otherwise.
     */
    [[nodiscard]] Result<QSet<QString>> searchConversations(const QString &text) const;

    /// The content of part `position` of the message. ErrorCode::NotFound when it stayed on the
    /// phone.
    [[nodiscard]] Result<QByteArray> attachmentData(quint32 luid, int position) const;

    /**
     * Archives the messages or takes them out of the archive. A message only
     * on this computer cannot leave the archive and fails with
     * ErrorCode::InvalidInput; nothing changes then.
     */
    [[nodiscard]] Result<void> setArchived(const QList<quint32> &luids, bool archived);

    /// Stores the read state after the phone took it.
    [[nodiscard]] Result<void> setRead(const QList<quint32> &luids, bool read);

    /**
     * Turns messages read from an archive file into messages for
     * importPrepared() and writes their parts to the blobs below
     * `profileDirectory`. Touches no database, so it runs on any thread.
     */
    [[nodiscard]] static Result<QList<StoredMessage>>
    prepareImport(const QList<codec::Message> &messages, const QString &profileDirectory);

    /**
     * Adds the messages as archived messages only on this computer. A message
     * equal in time, folder, other side and text to a stored one is skipped.
     * Returns how many were added.
     */
    [[nodiscard]] Result<int> importPrepared(const QList<StoredMessage> &messages);

    /**
     * Deletes messages that are only on this computer. Fails with
     * ErrorCode::InvalidInput for a message the phone still has; nothing
     * changes then.
     */
    [[nodiscard]] Result<void> removeLocal(const QList<quint32> &luids);

    /**
     * Stores one object fetched after this program wrote it, keeping the
     * change counter. Requires an earlier pull of the messages.
     */
    [[nodiscard]] Result<void> storeObject(const sync::RawObject &object);

    /// Removes one message after it was deleted on the phone.
    [[nodiscard]] Result<void> removeObject(quint32 luid);

    /// Whether the search uses the full-text index of SQLite.
    [[nodiscard]] bool hasFullTextSearch() const;

    /**
     * The key that groups messages into conversations: for a phone number its
     * last nine digits once leading zeros are gone, so that a country code
     * makes no difference; for a name such as a sender id the name in lower
     * case. Empty for an empty address.
     */
    [[nodiscard]] static QString threadKeyOf(const QString &address);

private:
    [[nodiscard]] Result<MessageChanges> writePrepared(const QString &connectionName,
                                                       const PreparedMessages &prepared);
    void pruneReleasedBlobs();

    ProfileStore *m_profile = nullptr;
    /// Blobs of rows the running change replaces or deletes.
    QSet<QString> m_releasedBlobs;
    mutable std::optional<bool> m_fullTextSearch;
};

} // namespace qpm::core::store
