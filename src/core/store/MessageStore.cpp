#include "core/store/MessageStore.h"

#include "core/Logging.h"
#include "core/store/ProfileStore.h"
#include "core/sync/IrmcSync.h"

#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <limits>
#include <utility>

namespace qpm::core::store {

namespace {

using codec::MessageBox;
using codec::MessageKind;
using sync::IrmcDomain;

constexpr qsizetype threadKeyDigits = 9;
constexpr qsizetype minimumThreadKeyDigits = 5;
constexpr qsizetype snippetLength = 120;

QString pendingKey()
{
    return QStringLiteral("messages.pending");
}

QString kindText(MessageKind kind)
{
    switch (kind) {
    case MessageKind::Mms:
        return QStringLiteral("mms");
    case MessageKind::Scheduled:
        return QStringLiteral("scheduled");
    case MessageKind::Sms:
        break;
    }
    return QStringLiteral("sms");
}

MessageKind kindFromText(const QString &text)
{
    if (text == QLatin1StringView("mms"))
        return MessageKind::Mms;
    if (text == QLatin1StringView("scheduled"))
        return MessageKind::Scheduled;
    return MessageKind::Sms;
}

QString boxText(MessageBox box)
{
    switch (box) {
    case MessageBox::Inbox:
        return QStringLiteral("inbox");
    case MessageBox::Sent:
        return QStringLiteral("sent");
    case MessageBox::Draft:
        return QStringLiteral("draft");
    case MessageBox::Outbox:
        return QStringLiteral("outbox");
    case MessageBox::Failed:
        return QStringLiteral("failed");
    case MessageBox::Queued:
        return QStringLiteral("queued");
    case MessageBox::Scheduled:
        return QStringLiteral("scheduled");
    case MessageBox::Other:
        break;
    }
    return QStringLiteral("other");
}

MessageBox boxFromText(const QString &text)
{
    static const QHash<QString, MessageBox> boxes = {
        {QStringLiteral("inbox"), MessageBox::Inbox},
        {QStringLiteral("sent"), MessageBox::Sent},
        {QStringLiteral("draft"), MessageBox::Draft},
        {QStringLiteral("outbox"), MessageBox::Outbox},
        {QStringLiteral("failed"), MessageBox::Failed},
        {QStringLiteral("queued"), MessageBox::Queued},
        {QStringLiteral("scheduled"), MessageBox::Scheduled},
    };
    return boxes.value(text, MessageBox::Other);
}

// SQLite stores a null QString as NULL; the text columns are NOT NULL.
QString text(const QString &value)
{
    return value.isNull() ? QString(QLatin1StringView("")) : value;
}

QString dateText(const QDateTime &date)
{
    return date.isValid() ? date.toUTC().toString(Qt::ISODate) : QString(QLatin1StringView(""));
}

QDateTime dateFromText(const QString &value)
{
    if (value.isEmpty())
        return {};
    return QDateTime::fromString(value, Qt::ISODate).toUTC();
}

Error queryError(const QString &what, const QSqlQuery &query)
{
    return Error(ErrorCode::IoFailure,
                 QStringLiteral("%1: %2").arg(what, query.lastError().text()));
}

QString likePattern(const QString &word)
{
    QString escaped = word;
    escaped.replace(u'\\', QLatin1StringView("\\\\"));
    escaped.replace(u'%', QLatin1StringView("\\%"));
    escaped.replace(u'_', QLatin1StringView("\\_"));
    return u'%' + escaped + u'%';
}

// Each word as a quoted prefix term, so that no character of the input acts
// as an operator of the query language.
QString fullTextQuery(const QStringList &words)
{
    QStringList terms;
    for (QString word : words) {
        word.replace(u'"', QLatin1StringView("\"\""));
        terms.append(u'"' + word + QLatin1StringView("\"*"));
    }
    return terms.join(u' ');
}

QString snippetOf(const QString &body, const QString &subject)
{
    const QString source = body.trimmed().isEmpty() ? subject : body;
    return source.simplified().left(snippetLength);
}

constexpr const char *messageColumns =
    "luid, kind, box, read, date_utc, addresses, subject, body, sim, archived, thread_key";

StoredMessage messageFromRow(const QSqlQuery &query)
{
    StoredMessage message;
    message.luid = query.value(0).toUInt();
    message.kind = kindFromText(query.value(1).toString());
    message.box = boxFromText(query.value(2).toString());
    message.incoming = message.box == MessageBox::Inbox;
    message.read = query.value(3).toBool();
    message.date = dateFromText(query.value(4).toString());
    const QString addresses = query.value(5).toString();
    message.addresses = addresses.isEmpty() ? QStringList() : addresses.split(u'\n');
    message.subject = query.value(6).toString();
    message.body = query.value(7).toString();
    message.simSlot = query.value(8).toInt();
    message.archived = query.value(9).toBool();
    message.threadKey = query.value(10).toString();
    return message;
}

StoredAttachment attachmentFromRow(const QSqlQuery &query, int offset)
{
    StoredAttachment attachment;
    attachment.position = query.value(offset).toInt();
    attachment.name = query.value(offset + 1).toString();
    attachment.mimeType = query.value(offset + 2).toString();
    attachment.size = query.value(offset + 3).toLongLong();
    attachment.blob = query.value(offset + 4).toString();
    return attachment;
}

bool sameContent(const StoredMessage &a, const StoredMessage &b)
{
    return a.kind == b.kind && a.box == b.box && a.read == b.read && a.date == b.date &&
           a.addresses == b.addresses && a.subject == b.subject && a.body == b.body &&
           a.simSlot == b.simSlot && a.attachments.size() == b.attachments.size();
}

Result<StoredMessage> storedFrom(const codec::Message &message, const QString &profileDirectory)
{
    StoredMessage stored;
    stored.luid = message.luid;
    stored.kind = message.kind;
    stored.box = message.box;
    stored.incoming = message.box == MessageBox::Inbox;
    stored.read = stored.incoming ? message.read : true;
    stored.date = message.date;
    stored.addresses = message.addresses;
    stored.subject = message.subject;
    stored.body = message.body;
    stored.simSlot = message.simSlot;
    stored.threadKey = MessageStore::threadKeyOf(message.addresses.value(0));
    stored.recipients = message.recipients;
    int position = 0;
    for (const codec::MessageAttachment &part : message.attachments) {
        StoredAttachment attachment;
        attachment.position = position++;
        attachment.name = part.name;
        attachment.mimeType = part.mimeType;
        attachment.size = part.size;
        if (!part.onPhoneOnly) {
            const Result<QString> blob = ProfileStore::putBlobIn(profileDirectory, part.data);
            if (!blob.hasValue())
                return blob.error();
            attachment.blob = blob.value();
        }
        stored.attachments.append(attachment);
    }
    return stored;
}

class Writer
{
public:
    Writer(QSqlDatabase db, bool fullTextSearch) : m_db(std::move(db)), m_fts(fullTextSearch) {}

    [[nodiscard]] Result<QSet<QString>> blobsOf(quint32 luid)
    {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "SELECT blob FROM attachments WHERE message_luid = :luid AND blob <> ''"));
        query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
        if (!query.exec())
            return queryError(QStringLiteral("Cannot read message parts"), query);
        QSet<QString> blobs;
        while (query.next())
            blobs.insert(query.value(0).toString());
        return blobs;
    }

    [[nodiscard]] Result<void> remove(quint32 luid)
    {
        QSqlQuery query(m_db);
        QStringList statements = {
            QStringLiteral("DELETE FROM messages WHERE luid = :luid"),
            QStringLiteral("DELETE FROM attachments WHERE message_luid = :luid")};
        if (m_fts)
            statements.append(QStringLiteral("DELETE FROM messages_fts WHERE rowid = :luid"));
        for (const QString &statement : std::as_const(statements)) {
            query.prepare(statement);
            query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
            if (!query.exec())
                return queryError(QStringLiteral("Cannot delete a message"), query);
        }
        return {};
    }

    [[nodiscard]] Result<void> rekey(quint32 from, quint32 to)
    {
        QSqlQuery query(m_db);
        QStringList statements = {
            QStringLiteral("UPDATE messages SET luid = :to WHERE luid = :from"),
            QStringLiteral("UPDATE attachments SET message_luid = :to WHERE message_luid = :from")};
        if (m_fts) {
            statements.append(QStringLiteral("DELETE FROM messages_fts WHERE rowid = :from"));
            statements.append(QStringLiteral(
                "INSERT INTO messages_fts (rowid, body, subject, addresses) SELECT luid, body, "
                "subject, addresses FROM messages WHERE luid = :to"));
        }
        for (const QString &statement : std::as_const(statements)) {
            query.prepare(statement);
            if (statement.contains(QLatin1StringView(":from")))
                query.bindValue(QStringLiteral(":from"), static_cast<qint64>(from));
            if (statement.contains(QLatin1StringView(":to")))
                query.bindValue(QStringLiteral(":to"), static_cast<qint64>(to));
            if (!query.exec())
                return queryError(QStringLiteral("Cannot keep an archived message"), query);
        }
        return {};
    }

    [[nodiscard]] Result<void> insert(const StoredMessage &message)
    {
        QSqlQuery query(m_db);
        query.prepare(QStringLiteral(
            "INSERT INTO messages (luid, kind, box, read, date_utc, addresses, subject, body, sim, "
            "archived, thread_key) VALUES (:luid, :kind, :box, :read, :date, :addresses, :subject, "
            ":body, :sim, :archived, :threadKey)"));
        query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(message.luid));
        query.bindValue(QStringLiteral(":kind"), kindText(message.kind));
        query.bindValue(QStringLiteral(":box"), boxText(message.box));
        query.bindValue(QStringLiteral(":read"), message.read ? 1 : 0);
        query.bindValue(QStringLiteral(":date"), dateText(message.date));
        query.bindValue(QStringLiteral(":addresses"), text(message.addresses.join(u'\n')));
        query.bindValue(QStringLiteral(":subject"), text(message.subject));
        query.bindValue(QStringLiteral(":body"), text(message.body));
        query.bindValue(QStringLiteral(":sim"), message.simSlot);
        query.bindValue(QStringLiteral(":archived"), message.archived ? 1 : 0);
        query.bindValue(QStringLiteral(":threadKey"), text(message.threadKey));
        if (!query.exec())
            return queryError(QStringLiteral("Cannot store a message"), query);

        query.prepare(QStringLiteral(
            "INSERT INTO attachments (message_luid, position, name, mime, size, blob) VALUES "
            "(:luid, :position, :name, :mime, :size, :blob)"));
        for (const StoredAttachment &attachment : message.attachments) {
            query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(message.luid));
            query.bindValue(QStringLiteral(":position"), attachment.position);
            query.bindValue(QStringLiteral(":name"), text(attachment.name));
            query.bindValue(QStringLiteral(":mime"), text(attachment.mimeType));
            query.bindValue(QStringLiteral(":size"), attachment.size);
            query.bindValue(QStringLiteral(":blob"), text(attachment.blob));
            if (!query.exec())
                return queryError(QStringLiteral("Cannot store a message part"), query);
        }

        if (m_fts) {
            query.prepare(
                QStringLiteral("INSERT INTO messages_fts (rowid, body, subject, addresses) "
                               "VALUES (:luid, :body, :subject, :addresses)"));
            query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(message.luid));
            query.bindValue(QStringLiteral(":body"), text(message.body));
            query.bindValue(QStringLiteral(":subject"), text(message.subject));
            query.bindValue(QStringLiteral(":addresses"), text(message.addresses.join(u' ')));
            if (!query.exec())
                return queryError(QStringLiteral("Cannot index a message"), query);
        }
        return {};
    }

    [[nodiscard]] Result<quint32> nextLocalLuid()
    {
        QSqlQuery query(m_db);
        if (!query.exec(QStringLiteral("SELECT MAX(luid) FROM messages")) || !query.next())
            return queryError(QStringLiteral("Cannot read the messages"), query);
        const qint64 highest = query.value(0).toLongLong();
        if (highest < qint64(localMessageLuidBase))
            return localMessageLuidBase;
        if (highest >= qint64(std::numeric_limits<quint32>::max()))
            return Error(ErrorCode::LimitExceeded, QStringLiteral("No free message number left"));
        return quint32(highest + 1);
    }

private:
    QSqlDatabase m_db;
    bool m_fts = false;
};

} // namespace

MessageStore::MessageStore(ProfileStore *profile) : m_profile(profile)
{
    Q_ASSERT(profile);
}

std::optional<quint64> MessageStore::changeCounter(IrmcDomain domain) const
{
    if (domain == IrmcDomain::Messages) {
        QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
        query.prepare(QStringLiteral("SELECT 1 FROM meta WHERE key = :key"));
        query.bindValue(QStringLiteral(":key"), pendingKey());
        // A failed read counts as behind, which loads everything again.
        if (!query.exec() || query.next())
            return std::nullopt;
    }
    return m_profile->changeCounter(domain);
}

std::optional<QByteArray> MessageStore::objectHash(IrmcDomain domain, quint32 luid) const
{
    return m_profile->objectHash(domain, luid);
}

Result<void> MessageStore::apply(IrmcDomain domain, const sync::SyncDelta &delta)
{
    if (domain != IrmcDomain::Messages)
        return m_profile->apply(domain, delta);
    return m_profile->applyWith(domain, delta, [](const QString &connectionName) -> Result<void> {
        QSqlQuery query(QSqlDatabase::database(connectionName, false));
        query.prepare(
            QStringLiteral("INSERT OR REPLACE INTO meta (key, value) VALUES (:key, '1')"));
        query.bindValue(QStringLiteral(":key"), pendingKey());
        if (!query.exec())
            return queryError(QStringLiteral("Cannot mark the messages as behind"), query);
        return {};
    });
}

QString MessageStore::profileDirectory() const
{
    return m_profile->directory();
}

Result<PreparedMessages> MessageStore::prepare(const sync::SyncDelta &delta,
                                               const QString &profileDirectory)
{
    PreparedMessages prepared;
    prepared.full = delta.full;
    prepared.deletes = delta.deletes;
    prepared.messages.reserve(delta.upserts.size());
    for (const sync::RawObject &object : delta.upserts) {
        const Result<codec::Message> parsed = codec::parseVMessage(object.data);
        if (!parsed.hasValue()) {
            // The object stays in the sync state; a later pull may bring a
            // readable version.
            qCWarning(lcStore) << "Message" << sync::IrmcSync::luidText(object.luid)
                               << "not readable:" << parsed.error().message();
            ++prepared.unreadable;
            continue;
        }
        codec::Message message = parsed.value();
        message.luid = object.luid;
        if (message.kind == MessageKind::Sms && codec::kindOfLuid(object.luid) == MessageKind::Mms)
            message.kind = MessageKind::Mms;
        const Result<StoredMessage> stored = storedFrom(message, profileDirectory);
        if (!stored.hasValue())
            return stored.error();
        prepared.messages.append(stored.value());
    }
    return prepared;
}

Result<MessageChanges> MessageStore::applyPrepared(const PreparedMessages &prepared)
{
    QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    if (!db.transaction())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot start a transaction"));
    m_releasedBlobs.clear();
    const Result<MessageChanges> written = writePrepared(m_profile->connectionName(), prepared);
    if (!written.hasValue()) {
        db.rollback();
        m_releasedBlobs.clear();
        return written;
    }
    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM meta WHERE key = :key"));
    query.bindValue(QStringLiteral(":key"), pendingKey());
    if (!query.exec()) {
        const Error error =
            queryError(QStringLiteral("Cannot mark the messages as current"), query);
        db.rollback();
        m_releasedBlobs.clear();
        return error;
    }
    if (!db.commit()) {
        db.rollback();
        m_releasedBlobs.clear();
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot commit the messages"));
    }
    pruneReleasedBlobs();
    MessageChanges changes = written.value();
    changes.unreadable = prepared.unreadable;
    return changes;
}

Result<MessageChanges> MessageStore::writePrepared(const QString &connectionName,
                                                   const PreparedMessages &prepared)
{
    const QSqlDatabase db = QSqlDatabase::database(connectionName, false);
    Writer writer(db, hasFullTextSearch());

    // The stored state of what this change touches; a full load touches all.
    QHash<quint32, StoredMessage> before;
    {
        QSqlQuery query(db);
        QSqlQuery parts(db);
        parts.prepare(
            QStringLiteral("SELECT COUNT(*) FROM attachments WHERE message_luid = :luid"));
        const auto remember = [&before, &parts](const StoredMessage &found) -> Result<void> {
            StoredMessage message = found;
            parts.bindValue(QStringLiteral(":luid"), static_cast<qint64>(message.luid));
            if (!parts.exec() || !parts.next())
                return queryError(QStringLiteral("Cannot read message parts"), parts);
            message.attachments.resize(parts.value(0).toInt());
            before.insert(message.luid, message);
            return {};
        };
        if (prepared.full) {
            query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE luid < :base")
                              .arg(QLatin1StringView(messageColumns)));
            query.bindValue(QStringLiteral(":base"), static_cast<qint64>(localMessageLuidBase));
            if (!query.exec())
                return queryError(QStringLiteral("Cannot read the messages"), query);
            while (query.next()) {
                if (const Result<void> kept = remember(messageFromRow(query)); !kept.hasValue())
                    return kept.error();
            }
        } else {
            QList<quint32> touched = prepared.deletes;
            for (const StoredMessage &message : prepared.messages)
                touched.append(message.luid);
            query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE luid = :luid")
                              .arg(QLatin1StringView(messageColumns)));
            for (const quint32 luid : std::as_const(touched)) {
                query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
                if (!query.exec())
                    return queryError(QStringLiteral("Cannot read a message"), query);
                if (!query.next())
                    continue;
                if (const Result<void> kept = remember(messageFromRow(query)); !kept.hasValue())
                    return kept.error();
            }
        }
    }

    MessageChanges changes;
    QSet<quint32> kept;
    for (const StoredMessage &message : prepared.messages)
        kept.insert(message.luid);

    QList<quint32> gone = prepared.deletes;
    if (prepared.full) {
        for (auto it = before.cbegin(); it != before.cend(); ++it) {
            if (!kept.contains(it.key()))
                gone.append(it.key());
        }
    }
    QSet<quint32> handled;
    for (const quint32 luid : std::as_const(gone)) {
        const auto old = before.constFind(luid);
        if (old == before.cend() || kept.contains(luid) || handled.contains(luid))
            continue;
        handled.insert(luid);
        ++changes.removed;
        if (old->archived) {
            const Result<quint32> local = writer.nextLocalLuid();
            if (!local.hasValue())
                return local.error();
            if (const Result<void> moved = writer.rekey(luid, local.value()); !moved.hasValue())
                return moved.error();
            continue;
        }
        const Result<QSet<QString>> blobs = writer.blobsOf(luid);
        if (!blobs.hasValue())
            return blobs.error();
        m_releasedBlobs.unite(blobs.value());
        if (const Result<void> removed = writer.remove(luid); !removed.hasValue())
            return removed.error();
    }

    for (StoredMessage message : prepared.messages) {
        const auto old = before.constFind(message.luid);
        if (old != before.cend()) {
            message.archived = old->archived;
            if (!sameContent(old.value(), message))
                ++changes.changed;
            const Result<QSet<QString>> blobs = writer.blobsOf(message.luid);
            if (!blobs.hasValue())
                return blobs.error();
            m_releasedBlobs.unite(blobs.value());
            if (const Result<void> removed = writer.remove(message.luid); !removed.hasValue())
                return removed.error();
        } else {
            ++changes.added;
            // A full load brings everything the phone keeps, not what just came in.
            if (!prepared.full && message.incoming && !message.read &&
                message.kind != MessageKind::Scheduled) {
                changes.arrived.append(message.luid);
            }
        }
        if (const Result<void> inserted = writer.insert(message); !inserted.hasValue())
            return inserted.error();
    }
    return changes;
}

Result<QList<ConversationSummary>> MessageStore::conversations() const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    if (!query.exec(QStringLiteral(
            "SELECT t.thread_key, m.addresses, substr(m.body, 1, 400), m.subject, t.last_date, "
            "EXISTS (SELECT 1 FROM attachments a WHERE a.message_luid = m.luid), t.unread, "
            "t.total, t.archived FROM (SELECT thread_key, MAX(date_utc) AS last_date, "
            "SUM(CASE WHEN read = 0 THEN 1 ELSE 0 END) AS unread, COUNT(*) AS total, "
            "SUM(archived) AS archived FROM messages WHERE kind <> 'scheduled' "
            "GROUP BY thread_key) t JOIN messages m ON m.luid = (SELECT luid FROM messages "
            "WHERE thread_key = t.thread_key AND kind <> 'scheduled' "
            "ORDER BY date_utc DESC, luid DESC LIMIT 1) "
            "ORDER BY t.last_date DESC, t.thread_key"))) {
        return queryError(QStringLiteral("Cannot read the conversations"), query);
    }
    QList<ConversationSummary> list;
    while (query.next()) {
        ConversationSummary summary;
        summary.threadKey = query.value(0).toString();
        summary.address = query.value(1).toString().section(u'\n', 0, 0);
        summary.snippet = snippetOf(query.value(2).toString(), query.value(3).toString());
        summary.lastDate = dateFromText(query.value(4).toString());
        summary.lastHasAttachments = query.value(5).toBool();
        summary.unread = query.value(6).toInt();
        summary.count = query.value(7).toInt();
        summary.archived = query.value(8).toInt();
        list.append(summary);
    }
    return list;
}

Result<QList<StoredMessage>> MessageStore::conversation(const QString &threadKey) const
{
    const QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE thread_key = :key AND "
                                 "kind <> 'scheduled' ORDER BY date_utc, luid")
                      .arg(QLatin1StringView(messageColumns)));
    query.bindValue(QStringLiteral(":key"), threadKey);
    if (!query.exec())
        return queryError(QStringLiteral("Cannot read a conversation"), query);
    QList<StoredMessage> messages;
    QHash<quint32, qsizetype> rowOf;
    while (query.next()) {
        rowOf.insert(query.value(0).toUInt(), messages.size());
        messages.append(messageFromRow(query));
    }
    query.prepare(QStringLiteral(
        "SELECT a.message_luid, a.position, a.name, a.mime, a.size, a.blob FROM attachments a "
        "JOIN messages m ON m.luid = a.message_luid WHERE m.thread_key = :key "
        "ORDER BY a.message_luid, a.position"));
    query.bindValue(QStringLiteral(":key"), threadKey);
    if (!query.exec())
        return queryError(QStringLiteral("Cannot read message parts"), query);
    while (query.next()) {
        const auto row = rowOf.constFind(query.value(0).toUInt());
        if (row != rowOf.cend())
            messages[row.value()].attachments.append(attachmentFromRow(query, 1));
    }
    return messages;
}

Result<QList<StoredMessage>> MessageStore::scheduledMessages() const
{
    const QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT %1 FROM messages WHERE kind = 'scheduled' "
                                   "ORDER BY date_utc, luid")
                        .arg(QLatin1StringView(messageColumns)))) {
        return queryError(QStringLiteral("Cannot read the scheduled messages"), query);
    }
    QList<StoredMessage> messages;
    while (query.next())
        messages.append(messageFromRow(query));

    // The recipients with their state are only in the text from the phone.
    query.prepare(
        QStringLiteral("SELECT raw FROM sync_objects WHERE domain = 'msg' AND luid = :luid"));
    for (StoredMessage &message : messages) {
        query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(message.luid));
        if (!query.exec())
            return queryError(QStringLiteral("Cannot read a scheduled message"), query);
        if (!query.next())
            continue;
        const Result<codec::Message> parsed = codec::parseVMessage(query.value(0).toByteArray());
        if (parsed.hasValue())
            message.recipients = parsed.value().recipients;
    }
    return messages;
}

Result<QList<StoredMessage>> MessageStore::allMessages() const
{
    const QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT %1 FROM messages WHERE kind <> 'scheduled' "
                                   "ORDER BY thread_key, date_utc, luid")
                        .arg(QLatin1StringView(messageColumns)))) {
        return queryError(QStringLiteral("Cannot read the messages"), query);
    }
    QList<StoredMessage> messages;
    QHash<quint32, qsizetype> rowOf;
    while (query.next()) {
        rowOf.insert(query.value(0).toUInt(), messages.size());
        messages.append(messageFromRow(query));
    }
    if (!query.exec(QStringLiteral("SELECT message_luid, position, name, mime, size, blob FROM "
                                   "attachments ORDER BY message_luid, position"))) {
        return queryError(QStringLiteral("Cannot read message parts"), query);
    }
    while (query.next()) {
        const auto row = rowOf.constFind(query.value(0).toUInt());
        if (row != rowOf.cend())
            messages[row.value()].attachments.append(attachmentFromRow(query, 1));
    }
    return messages;
}

Result<StoredMessage> MessageStore::message(quint32 luid) const
{
    const QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT %1 FROM messages WHERE luid = :luid")
                      .arg(QLatin1StringView(messageColumns)));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
    if (!query.exec())
        return queryError(QStringLiteral("Cannot read a message"), query);
    if (!query.next())
        return Error(ErrorCode::NotFound,
                     QStringLiteral("No message %1").arg(sync::IrmcSync::luidText(luid)));
    StoredMessage message = messageFromRow(query);
    query.prepare(QStringLiteral("SELECT position, name, mime, size, blob FROM attachments "
                                 "WHERE message_luid = :luid ORDER BY position"));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
    if (!query.exec())
        return queryError(QStringLiteral("Cannot read message parts"), query);
    while (query.next())
        message.attachments.append(attachmentFromRow(query, 0));
    return message;
}

Result<QSet<QString>> MessageStore::searchConversations(const QString &text) const
{
    const QStringList words = text.simplified().split(u' ', Qt::SkipEmptyParts);
    QSet<QString> found;
    if (words.isEmpty())
        return found;
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    if (hasFullTextSearch()) {
        query.prepare(QStringLiteral(
            "SELECT DISTINCT m.thread_key FROM messages_fts f JOIN messages m ON m.luid = f.rowid "
            "WHERE messages_fts MATCH :query AND m.kind <> 'scheduled'"));
        query.bindValue(QStringLiteral(":query"), fullTextQuery(words));
    } else {
        QStringList conditions;
        for (qsizetype i = 0; i < words.size(); ++i) {
            conditions.append(QStringLiteral("(body LIKE :w%1 ESCAPE '\\' OR subject LIKE :w%1 "
                                             "ESCAPE '\\' OR addresses LIKE :w%1 ESCAPE '\\')")
                                  .arg(i));
        }
        // Only the placeholder names come from this function; the words are bound.
        query.prepare(QStringLiteral("SELECT DISTINCT thread_key FROM messages WHERE "
                                     "kind <> 'scheduled' AND ") +
                      conditions.join(QLatin1StringView(" AND ")));
        for (qsizetype i = 0; i < words.size(); ++i)
            query.bindValue(QStringLiteral(":w%1").arg(i), likePattern(words.at(i)));
    }
    if (!query.exec())
        return queryError(QStringLiteral("Cannot search the messages"), query);
    while (query.next())
        found.insert(query.value(0).toString());
    return found;
}

Result<QByteArray> MessageStore::attachmentData(quint32 luid, int position) const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    query.prepare(QStringLiteral(
        "SELECT blob FROM attachments WHERE message_luid = :luid AND position = :position"));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
    query.bindValue(QStringLiteral(":position"), position);
    if (!query.exec())
        return queryError(QStringLiteral("Cannot read a message part"), query);
    if (!query.next() || query.value(0).toString().isEmpty())
        return Error(ErrorCode::NotFound,
                     QStringLiteral("The message part is not on this computer"));
    return m_profile->blob(query.value(0).toString());
}

Result<void> MessageStore::setArchived(const QList<quint32> &luids, bool archived)
{
    if (!archived) {
        for (const quint32 luid : luids) {
            if (luid >= localMessageLuidBase) {
                return Error(ErrorCode::InvalidInput,
                             QStringLiteral("A message only on this computer stays archived"));
            }
        }
    }
    QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    if (!db.transaction())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot start a transaction"));
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE messages SET archived = :archived WHERE luid = :luid"));
    for (const quint32 luid : luids) {
        query.bindValue(QStringLiteral(":archived"), archived ? 1 : 0);
        query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
        if (!query.exec()) {
            const Error error = queryError(QStringLiteral("Cannot archive a message"), query);
            db.rollback();
            return error;
        }
    }
    if (!db.commit())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot commit the archive"));
    return {};
}

Result<void> MessageStore::setRead(const QList<quint32> &luids, bool read)
{
    QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    if (!db.transaction())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot start a transaction"));
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE messages SET read = :read WHERE luid = :luid"));
    for (const quint32 luid : luids) {
        query.bindValue(QStringLiteral(":read"), read ? 1 : 0);
        query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
        if (!query.exec()) {
            const Error error = queryError(QStringLiteral("Cannot store the read state"), query);
            db.rollback();
            return error;
        }
    }
    if (!db.commit())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot commit the read state"));
    return {};
}

Result<QList<StoredMessage>> MessageStore::prepareImport(const QList<codec::Message> &messages,
                                                         const QString &profileDirectory)
{
    QList<StoredMessage> prepared;
    prepared.reserve(messages.size());
    for (codec::Message message : messages) {
        message.luid = 0;
        if (message.kind == MessageKind::Scheduled)
            continue;
        const Result<StoredMessage> stored = storedFrom(message, profileDirectory);
        if (!stored.hasValue())
            return stored.error();
        prepared.append(stored.value());
    }
    return prepared;
}

Result<int> MessageStore::importPrepared(const QList<StoredMessage> &messages)
{
    QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    if (!db.transaction())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot start a transaction"));
    Writer writer(db, hasFullTextSearch());
    const auto fail = [&db](const Error &error) -> Result<int> {
        db.rollback();
        return error;
    };
    QSqlQuery duplicate(db);
    duplicate.prepare(QStringLiteral(
        "SELECT 1 FROM messages WHERE thread_key = :key AND date_utc = :date AND box = :box AND "
        "body = :body LIMIT 1"));
    int added = 0;
    for (StoredMessage message : messages) {
        duplicate.bindValue(QStringLiteral(":key"), text(message.threadKey));
        duplicate.bindValue(QStringLiteral(":date"), dateText(message.date));
        duplicate.bindValue(QStringLiteral(":box"), boxText(message.box));
        duplicate.bindValue(QStringLiteral(":body"), text(message.body));
        if (!duplicate.exec())
            return fail(queryError(QStringLiteral("Cannot compare a message"), duplicate));
        if (duplicate.next())
            continue;
        const Result<quint32> luid = writer.nextLocalLuid();
        if (!luid.hasValue())
            return fail(luid.error());
        message.luid = luid.value();
        message.archived = true;
        if (const Result<void> inserted = writer.insert(message); !inserted.hasValue())
            return fail(inserted.error());
        ++added;
    }
    if (!db.commit())
        return fail(Error(ErrorCode::IoFailure, QStringLiteral("Cannot commit the import")));
    return added;
}

Result<void> MessageStore::removeLocal(const QList<quint32> &luids)
{
    for (const quint32 luid : luids) {
        if (luid < localMessageLuidBase) {
            return Error(ErrorCode::InvalidInput,
                         QStringLiteral("The phone still has this message"));
        }
    }
    QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    if (!db.transaction())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot start a transaction"));
    Writer writer(db, hasFullTextSearch());
    m_releasedBlobs.clear();
    for (const quint32 luid : luids) {
        const Result<QSet<QString>> blobs = writer.blobsOf(luid);
        Result<void> removed = blobs.hasValue() ? writer.remove(luid) : Result<void>(blobs.error());
        if (!removed.hasValue()) {
            db.rollback();
            m_releasedBlobs.clear();
            return removed;
        }
        m_releasedBlobs.unite(blobs.value());
    }
    if (!db.commit()) {
        db.rollback();
        m_releasedBlobs.clear();
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot commit the deletion"));
    }
    pruneReleasedBlobs();
    return {};
}

Result<void> MessageStore::storeObject(const sync::RawObject &object)
{
    const std::optional<quint64> counter = m_profile->changeCounter(IrmcDomain::Messages);
    if (!counter) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("The messages were never pulled, so nothing can be stored"));
    }
    sync::SyncDelta delta;
    delta.upserts.append(object);
    delta.newCounter = *counter;
    const Result<PreparedMessages> prepared = prepare(delta, m_profile->directory());
    if (!prepared.hasValue())
        return prepared.error();
    if (prepared.value().messages.isEmpty())
        return Error(ErrorCode::ProtocolError,
                     QStringLiteral("The phone sent an unreadable message"));
    m_releasedBlobs.clear();
    const Result<void> applied = m_profile->applyWith(
        IrmcDomain::Messages, delta,
        [this, &prepared](const QString &connectionName) -> Result<void> {
            const Result<MessageChanges> written = writePrepared(connectionName, prepared.value());
            return written.hasValue() ? Result<void>() : Result<void>(written.error());
        });
    if (applied.hasValue())
        pruneReleasedBlobs();
    m_releasedBlobs.clear();
    return applied;
}

Result<void> MessageStore::removeObject(quint32 luid)
{
    const std::optional<quint64> counter = m_profile->changeCounter(IrmcDomain::Messages);
    if (!counter) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("The messages were never pulled, so nothing can be removed"));
    }
    sync::SyncDelta delta;
    delta.deletes.append(luid);
    delta.newCounter = *counter;
    PreparedMessages prepared;
    prepared.deletes.append(luid);
    m_releasedBlobs.clear();
    const Result<void> applied = m_profile->applyWith(
        IrmcDomain::Messages, delta,
        [this, &prepared](const QString &connectionName) -> Result<void> {
            const Result<MessageChanges> written = writePrepared(connectionName, prepared);
            return written.hasValue() ? Result<void>() : Result<void>(written.error());
        });
    if (applied.hasValue())
        pruneReleasedBlobs();
    m_releasedBlobs.clear();
    return applied;
}

bool MessageStore::hasFullTextSearch() const
{
    if (!m_fullTextSearch) {
        QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
        query.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE name = :name"));
        query.bindValue(QStringLiteral(":name"), QStringLiteral("messages_fts"));
        if (!query.exec()) {
            // Searching without the index still works, only slower.
            qCWarning(lcStore) << "Cannot look for the search index:" << query.lastError().text();
            return false;
        }
        m_fullTextSearch = query.next();
    }
    return *m_fullTextSearch;
}

QString MessageStore::threadKeyOf(const QString &address)
{
    const QString trimmed = address.trimmed();
    QString digits;
    bool letters = false;
    for (const QChar c : trimmed) {
        if (c.isDigit())
            digits.append(c);
        else if (c.isLetter())
            letters = true;
    }
    if (!letters) {
        qsizetype zeros = 0;
        while (zeros < digits.size() && digits.at(zeros) == u'0')
            ++zeros;
        digits.remove(0, zeros);
        if (digits.size() >= minimumThreadKeyDigits)
            return digits.right(threadKeyDigits);
    }
    return trimmed.toLower();
}

void MessageStore::pruneReleasedBlobs()
{
    const QSet<QString> released = std::exchange(m_releasedBlobs, {});
    if (released.isEmpty())
        return;
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    // A contact photo with the same content shares the blob.
    query.prepare(QStringLiteral("SELECT 1 FROM attachments WHERE blob = :part UNION ALL "
                                 "SELECT 1 FROM contacts WHERE photo_blob = :photo LIMIT 1"));
    for (const QString &blob : released) {
        query.bindValue(QStringLiteral(":part"), blob);
        query.bindValue(QStringLiteral(":photo"), blob);
        if (!query.exec()) {
            // A part kept by mistake only takes space; the next change that
            // releases it tries again.
            qCWarning(lcStore) << "Cannot check whether a message part is still used:"
                               << query.lastError().text();
            continue;
        }
        if (query.next())
            continue;
        const Result<void> removed = m_profile->removeBlob(blob);
        if (!removed.hasValue()) {
            // Same as above: the file stays and costs only space.
            qCWarning(lcStore) << "Unused message part not deleted:" << removed.error().message();
        }
    }
}

} // namespace qpm::core::store
