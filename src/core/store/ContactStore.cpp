#include "core/store/ContactStore.h"

#include "core/Logging.h"
#include "core/store/ProfileStore.h"
#include "core/sync/IrmcSync.h"

#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <utility>

namespace qpm::core::store {

namespace {

using codec::Contact;
using codec::ContactValue;
using codec::ContactValueKind;

constexpr qsizetype minimumDigitsForNumberSearch = 3;
constexpr qsizetype numberKeyDigits = 9;
constexpr qsizetype minimumNumberKeyDigits = 5;

QString kindText(ContactValueKind kind)
{
    switch (kind) {
    case ContactValueKind::Phone:
        return QStringLiteral("phone");
    case ContactValueKind::Email:
        return QStringLiteral("email");
    case ContactValueKind::Address:
        return QStringLiteral("address");
    case ContactValueKind::Website:
        return QStringLiteral("website");
    case ContactValueKind::Im:
        return QStringLiteral("im");
    case ContactValueKind::Event:
        break;
    }
    return QStringLiteral("event");
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

QString digitsOf(const QString &text)
{
    QString digits;
    for (const QChar c : text) {
        if (c.isDigit())
            digits.append(c);
    }
    return digits;
}

// Lower case without accents, so that a search for "muller" finds "Müller".
QString searchForm(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing)
            out.append(c.toLower());
    }
    return out;
}

QString likePattern(const QString &text)
{
    QString escaped = text;
    escaped.replace(u'\\', QLatin1StringView("\\\\"));
    escaped.replace(u'%', QLatin1StringView("\\%"));
    escaped.replace(u'_', QLatin1StringView("\\_"));
    return u'%' + escaped + u'%';
}

QString addressText(const ContactValue &entry)
{
    QStringList parts;
    for (const QString &part : entry.address) {
        if (!part.trimmed().isEmpty())
            parts.append(part.trimmed());
    }
    return parts.join(QLatin1StringView(", "));
}

struct ValueRow
{
    QString kind;
    quint64 dataId = 0;
    int type = 0;
    QString label;
    QString value;
    bool primary = false;
};

QList<ValueRow> valueRows(const Contact &contact)
{
    QList<ValueRow> rows;
    QStringList searchable = {codec::displayNameOf(contact), contact.nickname,
                              contact.organization};
    for (const ContactValueKind kind : codec::allContactValueKinds) {
        for (const ContactValue &entry : contact.values(kind)) {
            const QString value =
                kind == ContactValueKind::Address ? addressText(entry) : entry.value.trimmed();
            if (value.isEmpty())
                continue;
            rows.append(
                {kindText(kind), entry.dataId, entry.type, entry.label, value, entry.primary});
            if (kind == ContactValueKind::Phone) {
                const QString digits = digitsOf(value);
                if (!digits.isEmpty())
                    rows.append({QStringLiteral("digits"), 0, 0, QString(), digits, false});
            } else if (kind == ContactValueKind::Email) {
                searchable.append(value);
            }
        }
    }
    for (const QString &group : contact.groups)
        rows.append({QStringLiteral("group"), 0, 0, QString(), group, false});
    if (!contact.organization.isEmpty())
        rows.append({QStringLiteral("organization"), 0, 0, QString(), contact.organization, false});
    rows.append(
        {QStringLiteral("search"), 0, 0, QString(), searchForm(searchable.join(u'\n')), false});
    return rows;
}

Result<QSet<quint32>> storedLuids(const QSqlDatabase &db)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT luid FROM contacts")))
        return queryError(QStringLiteral("Cannot read the contacts"), query);
    QSet<quint32> luids;
    while (query.next())
        luids.insert(query.value(0).toUInt());
    return luids;
}

Result<void> deleteContact(const QSqlDatabase &db, quint32 luid)
{
    QSqlQuery query(db);
    for (const QString &statement :
         {QStringLiteral("DELETE FROM contacts WHERE luid = :luid"),
          QStringLiteral("DELETE FROM contact_values WHERE luid = :luid")}) {
        query.prepare(statement);
        query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
        if (!query.exec())
            return queryError(QStringLiteral("Cannot delete a contact"), query);
    }
    return {};
}

// The photo blobs of the contacts `luids`, or of every contact with `all`.
Result<QSet<QString>> photoBlobs(const QSqlDatabase &db, const QList<quint32> &luids, bool all)
{
    QSet<QString> blobs;
    QSqlQuery query(db);
    if (all) {
        if (!query.exec(QStringLiteral("SELECT photo_blob FROM contacts WHERE photo_blob <> ''")))
            return queryError(QStringLiteral("Cannot read the photos"), query);
        while (query.next())
            blobs.insert(query.value(0).toString());
        return blobs;
    }
    query.prepare(
        QStringLiteral("SELECT photo_blob FROM contacts WHERE luid = :luid AND photo_blob <> ''"));
    for (const quint32 luid : luids) {
        query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
        if (!query.exec())
            return queryError(QStringLiteral("Cannot read a photo"), query);
        while (query.next())
            blobs.insert(query.value(0).toString());
    }
    return blobs;
}

Result<void> writeMetadata(const QSqlDatabase &db, const codec::PbMetadata &metadata)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("DELETE FROM accounts")) ||
        !query.exec(QStringLiteral("DELETE FROM \"groups\""))) {
        return queryError(QStringLiteral("Cannot clear the accounts"), query);
    }
    query.prepare(
        QStringLiteral("INSERT OR REPLACE INTO accounts (type, name, title, selected, "
                       "is_default) VALUES (:type, :name, :title, :selected, :isDefault)"));
    for (const codec::PbAccount &account : metadata.accounts) {
        query.bindValue(QStringLiteral(":type"), text(account.type));
        query.bindValue(QStringLiteral(":name"), text(account.name));
        query.bindValue(QStringLiteral(":title"), text(account.title));
        query.bindValue(QStringLiteral(":selected"), account.selected ? 1 : 0);
        query.bindValue(QStringLiteral(":isDefault"), account.isDefault ? 1 : 0);
        if (!query.exec())
            return queryError(QStringLiteral("Cannot store an account"), query);
    }
    query.prepare(QStringLiteral("INSERT INTO \"groups\" (title, source_id, account_type, "
                                 "account_name) VALUES (:title, :sourceId, :type, :name)"));
    for (const codec::PbGroup &group : metadata.groups) {
        query.bindValue(QStringLiteral(":title"), text(group.title));
        query.bindValue(QStringLiteral(":sourceId"), text(group.sourceId));
        query.bindValue(QStringLiteral(":type"), text(group.accountType));
        query.bindValue(QStringLiteral(":name"), text(group.accountName));
        if (!query.exec())
            return queryError(QStringLiteral("Cannot store a group"), query);
    }
    return {};
}

} // namespace

ContactStore::ContactStore(ProfileStore *profile) : m_profile(profile)
{
    Q_ASSERT(profile);
}

std::optional<quint64> ContactStore::changeCounter(sync::IrmcDomain domain) const
{
    return m_profile->changeCounter(domain);
}

std::optional<QByteArray> ContactStore::objectHash(sync::IrmcDomain domain, quint32 luid) const
{
    return m_profile->objectHash(domain, luid);
}

Result<void> ContactStore::apply(sync::IrmcDomain domain, const sync::SyncDelta &delta)
{
    if (domain != sync::IrmcDomain::Contacts)
        return m_profile->apply(domain, delta);

    const QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    const Result<QSet<quint32>> before = storedLuids(db);
    if (!before.hasValue())
        return before.error();
    ContactChanges changes;
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

    m_releasedBlobs.clear();
    const Result<void> applied = m_profile->applyWith(
        domain, delta, [this, &delta](const QString &connectionName) -> Result<void> {
            if (const Result<void> rows = writeRows(connectionName, delta); !rows.hasValue())
                return rows;
            if (delta.metadata.isEmpty())
                return {};
            const Result<codec::PbMetadata> metadata = codec::parsePbMetadata(delta.metadata);
            if (!metadata.hasValue()) {
                // The contacts stay usable without accounts and groups.
                qCWarning(lcStore)
                    << "Phonebook metadata not stored:" << metadata.error().message();
                return {};
            }
            return writeMetadata(QSqlDatabase::database(connectionName, false), metadata.value());
        });
    if (applied.hasValue()) {
        m_lastChanges = changes;
        pruneReleasedBlobs();
    }
    m_releasedBlobs.clear();
    return applied;
}

Result<void> ContactStore::writeRows(const QString &connectionName, const sync::SyncDelta &delta)
{
    const QSqlDatabase db = QSqlDatabase::database(connectionName, false);
    QList<quint32> replaced = delta.deletes;
    if (!delta.full) {
        for (const sync::RawObject &object : delta.upserts)
            replaced.append(object.luid);
    }
    const Result<QSet<QString>> released = photoBlobs(db, replaced, delta.full);
    if (!released.hasValue())
        return released.error();
    m_releasedBlobs.unite(released.value());

    QSqlQuery query(db);
    if (delta.full) {
        if (!query.exec(QStringLiteral("DELETE FROM contacts")) ||
            !query.exec(QStringLiteral("DELETE FROM contact_values"))) {
            return queryError(QStringLiteral("Cannot clear the contacts"), query);
        }
    }
    for (const quint32 luid : delta.deletes) {
        if (const Result<void> removed = deleteContact(db, luid); !removed.hasValue())
            return removed;
    }

    QSqlQuery insertContact(db);
    insertContact.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO contacts (luid, account_type, account_name, display_name, "
        "sort_key, vcard, hash, photo_blob) VALUES (:luid, :accountType, :accountName, "
        ":displayName, :sortKey, :vcard, :hash, :photoBlob)"));
    QSqlQuery insertValue(db);
    insertValue.prepare(QStringLiteral(
        "INSERT INTO contact_values (luid, kind, data_id, type, label, value, is_primary) "
        "VALUES (:luid, :kind, :dataId, :type, :label, :value, :primary)"));

    for (const sync::RawObject &object : delta.upserts) {
        if (!delta.full) {
            if (const Result<void> removed = deleteContact(db, object.luid); !removed.hasValue())
                return removed;
        }
        const Result<Contact> parsed = codec::parsePhoneVCard(object.data);
        if (!parsed.hasValue()) {
            // The object stays in the sync state; a later pull may bring a
            // readable version.
            qCWarning(lcStore) << "Contact" << sync::IrmcSync::luidText(object.luid)
                               << "not readable:" << parsed.error().message();
            continue;
        }
        const Contact &contact = parsed.value();
        QString photoBlob;
        if (!contact.photo.isEmpty()) {
            const Result<QString> blob = m_profile->putBlob(contact.photo);
            if (!blob.hasValue())
                return blob.error();
            photoBlob = blob.value();
        }
        insertContact.bindValue(QStringLiteral(":luid"), static_cast<qint64>(object.luid));
        insertContact.bindValue(QStringLiteral(":accountType"), text(contact.accountType));
        insertContact.bindValue(QStringLiteral(":accountName"), text(contact.accountName));
        insertContact.bindValue(QStringLiteral(":displayName"),
                                text(codec::displayNameOf(contact)));
        insertContact.bindValue(QStringLiteral(":sortKey"), text(codec::sortKeyOf(contact)));
        insertContact.bindValue(QStringLiteral(":vcard"), object.data);
        insertContact.bindValue(QStringLiteral(":hash"), object.hash);
        insertContact.bindValue(QStringLiteral(":photoBlob"), text(photoBlob));
        if (!insertContact.exec())
            return queryError(QStringLiteral("Cannot store a contact"), insertContact);

        const QList<ValueRow> rows = valueRows(contact);
        for (const ValueRow &row : rows) {
            insertValue.bindValue(QStringLiteral(":luid"), static_cast<qint64>(object.luid));
            insertValue.bindValue(QStringLiteral(":kind"), text(row.kind));
            insertValue.bindValue(QStringLiteral(":dataId"), static_cast<qint64>(row.dataId));
            insertValue.bindValue(QStringLiteral(":type"), row.type);
            insertValue.bindValue(QStringLiteral(":label"), text(row.label));
            insertValue.bindValue(QStringLiteral(":value"), text(row.value));
            insertValue.bindValue(QStringLiteral(":primary"), row.primary ? 1 : 0);
            if (!insertValue.exec())
                return queryError(QStringLiteral("Cannot store a contact entry"), insertValue);
        }
    }
    return {};
}

Result<QList<ContactSummary>> ContactStore::summaries() const
{
    const QSqlDatabase db = QSqlDatabase::database(m_profile->connectionName(), false);
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT luid, display_name, sort_key, account_type, "
                                   "account_name, photo_blob FROM contacts "
                                   "ORDER BY sort_key, luid"))) {
        return queryError(QStringLiteral("Cannot read the contacts"), query);
    }
    QList<ContactSummary> list;
    QHash<quint32, qsizetype> rowOf;
    while (query.next()) {
        ContactSummary summary;
        summary.luid = query.value(0).toUInt();
        summary.displayName = query.value(1).toString();
        summary.sortKey = query.value(2).toString();
        summary.accountType = query.value(3).toString();
        summary.accountName = query.value(4).toString();
        summary.photoBlob = query.value(5).toString();
        rowOf.insert(summary.luid, list.size());
        list.append(summary);
    }

    if (!query.exec(QStringLiteral("SELECT luid, kind, value, is_primary FROM contact_values "
                                   "WHERE kind IN ('phone', 'email', 'group', 'organization') "
                                   "ORDER BY rowid"))) {
        return queryError(QStringLiteral("Cannot read the contact entries"), query);
    }
    QSet<quint32> primaryNumberSeen;
    while (query.next()) {
        const auto row = rowOf.constFind(query.value(0).toUInt());
        if (row == rowOf.cend())
            continue;
        ContactSummary &summary = list[row.value()];
        const QString kind = query.value(1).toString();
        const QString value = query.value(2).toString();
        const bool primary = query.value(3).toBool();
        if (kind == QLatin1StringView("phone")) {
            if (summary.primaryNumber.isEmpty() ||
                (primary && !primaryNumberSeen.contains(summary.luid))) {
                summary.primaryNumber = value;
                if (primary)
                    primaryNumberSeen.insert(summary.luid);
            }
        } else if (kind == QLatin1StringView("email")) {
            if (summary.primaryEmail.isEmpty())
                summary.primaryEmail = value;
        } else if (kind == QLatin1StringView("group")) {
            summary.groups.append(value);
        } else {
            summary.organization = value;
        }
    }
    return list;
}

Result<Contact> ContactStore::contact(quint32 luid) const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    query.prepare(QStringLiteral("SELECT vcard FROM contacts WHERE luid = :luid"));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
    if (!query.exec())
        return queryError(QStringLiteral("Cannot read a contact"), query);
    if (!query.next())
        return Error(ErrorCode::NotFound,
                     QStringLiteral("No contact %1").arg(sync::IrmcSync::luidText(luid)));
    Result<Contact> parsed = codec::parsePhoneVCard(query.value(0).toByteArray());
    if (!parsed.hasValue())
        return parsed;
    Contact contact = parsed.value();
    contact.luid = luid;
    return contact;
}

Result<QList<Contact>> ContactStore::allContacts() const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    if (!query.exec(QStringLiteral("SELECT luid, vcard FROM contacts ORDER BY sort_key, luid")))
        return queryError(QStringLiteral("Cannot read the contacts"), query);
    QList<Contact> contacts;
    while (query.next()) {
        const quint32 luid = query.value(0).toUInt();
        const Result<Contact> parsed = codec::parsePhoneVCard(query.value(1).toByteArray());
        if (!parsed.hasValue()) {
            // The list goes on without the entry; its stored row stays as it is.
            qCWarning(lcStore) << "Contact" << sync::IrmcSync::luidText(luid)
                               << "not readable:" << parsed.error().message();
            continue;
        }
        Contact contact = parsed.value();
        contact.luid = luid;
        contacts.append(contact);
    }
    return contacts;
}

Result<QSet<quint32>> ContactStore::search(const QString &text) const
{
    const QString trimmed = text.trimmed();
    QSet<quint32> found;
    if (trimmed.isEmpty())
        return found;
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    const QString digits = digitsOf(trimmed);
    const bool byNumber = digits.size() >= minimumDigitsForNumberSearch;
    query.prepare(byNumber ? QStringLiteral("SELECT DISTINCT luid FROM contact_values WHERE "
                                            "(kind = 'search' AND value LIKE :text ESCAPE '\\') OR "
                                            "(kind = 'digits' AND value LIKE :digits ESCAPE '\\')")
                           : QStringLiteral("SELECT DISTINCT luid FROM contact_values WHERE "
                                            "kind = 'search' AND value LIKE :text ESCAPE '\\'"));
    query.bindValue(QStringLiteral(":text"), likePattern(searchForm(trimmed)));
    if (byNumber)
        query.bindValue(QStringLiteral(":digits"), likePattern(digits));
    if (!query.exec())
        return queryError(QStringLiteral("Cannot search the contacts"), query);
    while (query.next())
        found.insert(query.value(0).toUInt());
    return found;
}

QString ContactStore::numberKey(const QString &number)
{
    QString digits = digitsOf(number);
    qsizetype zeros = 0;
    while (zeros < digits.size() && digits.at(zeros) == u'0')
        ++zeros;
    digits.remove(0, zeros);
    if (digits.size() < minimumNumberKeyDigits)
        return {};
    return digits.right(numberKeyDigits);
}

Result<QHash<QString, QString>> ContactStore::namesByNumber() const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    if (!query.exec(QStringLiteral("SELECT c.display_name, v.value FROM contact_values v "
                                   "JOIN contacts c ON c.luid = v.luid WHERE v.kind = 'digits' "
                                   "ORDER BY c.sort_key, c.luid"))) {
        return queryError(QStringLiteral("Cannot read the contact numbers"), query);
    }
    QHash<QString, QString> names;
    while (query.next()) {
        const QString key = numberKey(query.value(1).toString());
        if (!key.isEmpty() && !names.contains(key))
            names.insert(key, query.value(0).toString());
    }
    return names;
}

Result<QHash<QString, QStringList>> ContactStore::sharedNumberNames() const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    if (!query.exec(QStringLiteral("SELECT c.display_name, v.value, c.luid FROM contact_values v "
                                   "JOIN contacts c ON c.luid = v.luid WHERE v.kind = 'digits' "
                                   "ORDER BY c.sort_key, c.luid"))) {
        return queryError(QStringLiteral("Cannot read the contact numbers"), query);
    }
    QHash<QString, QStringList> names;
    QHash<QString, QSet<quint32>> owners;
    while (query.next()) {
        const QString key = numberKey(query.value(1).toString());
        const quint32 luid = query.value(2).toUInt();
        if (key.isEmpty() || owners.value(key).contains(luid))
            continue;
        owners[key].insert(luid);
        names[key].append(query.value(0).toString());
    }
    QHash<QString, QStringList> shared;
    for (auto entry = names.cbegin(); entry != names.cend(); ++entry) {
        if (entry.value().size() > 1)
            shared.insert(entry.key(), entry.value());
    }
    return shared;
}

Result<codec::PbMetadata> ContactStore::metadata() const
{
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    codec::PbMetadata metadata;
    if (!query.exec(QStringLiteral("SELECT type, name, title, selected, is_default FROM accounts "
                                   "ORDER BY is_default DESC, title, name"))) {
        return queryError(QStringLiteral("Cannot read the accounts"), query);
    }
    while (query.next()) {
        metadata.accounts.append({query.value(0).toString(), query.value(1).toString(),
                                  query.value(2).toString(), query.value(3).toBool(),
                                  query.value(4).toBool()});
    }
    if (!query.exec(QStringLiteral("SELECT title, source_id, account_type, account_name FROM "
                                   "\"groups\" ORDER BY title"))) {
        return queryError(QStringLiteral("Cannot read the groups"), query);
    }
    while (query.next()) {
        metadata.groups.append({query.value(0).toString(), query.value(1).toString(),
                                query.value(2).toString(), query.value(3).toString()});
    }
    return metadata;
}

Result<void> ContactStore::storeObject(const sync::RawObject &object)
{
    const std::optional<quint64> counter = changeCounter(sync::IrmcDomain::Contacts);
    if (!counter) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("The phonebook was never pulled, so nothing can be stored"));
    }
    sync::SyncDelta delta;
    delta.upserts.append(object);
    delta.newCounter = *counter;
    m_releasedBlobs.clear();
    const Result<void> applied = m_profile->applyWith(
        sync::IrmcDomain::Contacts, delta,
        [this, &delta](const QString &connectionName) { return writeRows(connectionName, delta); });
    if (applied.hasValue())
        pruneReleasedBlobs();
    m_releasedBlobs.clear();
    return applied;
}

Result<void> ContactStore::removeObject(quint32 luid)
{
    const std::optional<quint64> counter = changeCounter(sync::IrmcDomain::Contacts);
    if (!counter) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("The phonebook was never pulled, so nothing can be removed"));
    }
    sync::SyncDelta delta;
    delta.deletes.append(luid);
    delta.newCounter = *counter;
    m_releasedBlobs.clear();
    const Result<void> applied = m_profile->applyWith(
        sync::IrmcDomain::Contacts, delta,
        [this, &delta](const QString &connectionName) { return writeRows(connectionName, delta); });
    if (applied.hasValue())
        pruneReleasedBlobs();
    m_releasedBlobs.clear();
    return applied;
}

void ContactStore::pruneReleasedBlobs()
{
    const QSet<QString> released = std::exchange(m_releasedBlobs, {});
    if (released.isEmpty())
        return;
    QSqlQuery query(QSqlDatabase::database(m_profile->connectionName(), false));
    // A message part with the same content as a photo shares its blob.
    query.prepare(QStringLiteral("SELECT 1 FROM contacts WHERE photo_blob = :photo UNION ALL "
                                 "SELECT 1 FROM attachments WHERE blob = :part LIMIT 1"));
    for (const QString &blob : released) {
        query.bindValue(QStringLiteral(":photo"), blob);
        query.bindValue(QStringLiteral(":part"), blob);
        if (!query.exec()) {
            // A photo kept by mistake only takes space; the next change that
            // releases it tries again.
            qCWarning(lcStore) << "Cannot check whether a photo is still used:"
                               << query.lastError().text();
            continue;
        }
        if (query.next())
            continue;
        const Result<void> removed = m_profile->removeBlob(blob);
        if (!removed.hasValue()) {
            // Same as above: the file stays and costs only space.
            qCWarning(lcStore) << "Unused photo not deleted:" << removed.error().message();
        }
    }
}

} // namespace qpm::core::store
