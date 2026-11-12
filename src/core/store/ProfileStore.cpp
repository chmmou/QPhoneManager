#include "core/store/ProfileStore.h"

#include "core/Logging.h"
#include "core/sync/IrmcSync.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>

namespace qpm::core::store {

namespace {

QString driverName()
{
    return QStringLiteral("QSQLITE");
}

QString databaseFile()
{
    return QStringLiteral("data.sqlite");
}

QString backupFile()
{
    return QStringLiteral("data.sqlite.bak");
}

QString blobsFolder()
{
    return QStringLiteral("blobs");
}

QString profilePrefix()
{
    return QStringLiteral("profile.");
}

constexpr qsizetype maxDirectoryNameLength = 64;
constexpr qsizetype maxDisplayNameLength = 64;
constexpr qsizetype sha1HexLength = 40;

// Index i holds the statements that take the schema from version i to i + 1.
const QList<QStringList> &migrations()
{
    static const QList<QStringList> steps = {
        {
            QStringLiteral("CREATE TABLE meta (key TEXT PRIMARY KEY NOT NULL, value TEXT)"),
            QStringLiteral("CREATE TABLE sync_objects (domain TEXT NOT NULL, luid INTEGER NOT NULL, "
                           "hash BLOB NOT NULL, raw BLOB NOT NULL, PRIMARY KEY (domain, luid))"),
        },
        {
            QStringLiteral("CREATE TABLE contacts (luid INTEGER PRIMARY KEY NOT NULL, "
                           "account_type TEXT NOT NULL, account_name TEXT NOT NULL, "
                           "display_name TEXT NOT NULL, sort_key TEXT NOT NULL, "
                           "vcard BLOB NOT NULL, hash BLOB NOT NULL, photo_blob TEXT NOT NULL)"),
            QStringLiteral("CREATE INDEX contacts_sort_key ON contacts (sort_key)"),
            QStringLiteral("CREATE TABLE contact_values (luid INTEGER NOT NULL, kind TEXT NOT NULL, "
                           "data_id INTEGER NOT NULL, type INTEGER NOT NULL, label TEXT NOT NULL, "
                           "value TEXT NOT NULL, is_primary INTEGER NOT NULL)"),
            QStringLiteral("CREATE INDEX contact_values_luid ON contact_values (luid)"),
            QStringLiteral("CREATE INDEX contact_values_value ON contact_values (value)"),
            QStringLiteral("CREATE TABLE accounts (type TEXT NOT NULL, name TEXT NOT NULL, "
                           "title TEXT NOT NULL, selected INTEGER NOT NULL, "
                           "is_default INTEGER NOT NULL, PRIMARY KEY (type, name))"),
            QStringLiteral("CREATE TABLE \"groups\" (title TEXT NOT NULL, source_id TEXT NOT NULL, "
                           "account_type TEXT NOT NULL, account_name TEXT NOT NULL)"),
            // Contacts pulled before these tables existed are only in
            // sync_objects; forgetting them makes the next pull load all.
            QStringLiteral("DELETE FROM sync_objects WHERE domain = 'pb'"),
            QStringLiteral("DELETE FROM meta WHERE key = 'counter.pb'"),
        },
        {
            QStringLiteral("CREATE TABLE calls (id INTEGER PRIMARY KEY NOT NULL, "
                           "number TEXT NOT NULL, name TEXT NOT NULL, direction TEXT NOT NULL, "
                           "start_utc TEXT NOT NULL, duration INTEGER NOT NULL, "
                           "sim INTEGER NOT NULL)"),
            QStringLiteral("CREATE INDEX calls_start ON calls (start_utc)"),
        },
        {
            QStringLiteral("CREATE TABLE messages (luid INTEGER PRIMARY KEY NOT NULL, "
                           "kind TEXT NOT NULL, box TEXT NOT NULL, read INTEGER NOT NULL, "
                           "date_utc TEXT NOT NULL, addresses TEXT NOT NULL, "
                           "subject TEXT NOT NULL, body TEXT NOT NULL, sim INTEGER NOT NULL, "
                           "archived INTEGER NOT NULL, thread_key TEXT NOT NULL)"),
            QStringLiteral("CREATE INDEX messages_thread ON messages (thread_key, date_utc)"),
            QStringLiteral("CREATE INDEX messages_date ON messages (date_utc)"),
            QStringLiteral("CREATE TABLE attachments (message_luid INTEGER NOT NULL, "
                           "position INTEGER NOT NULL, name TEXT NOT NULL, mime TEXT NOT NULL, "
                           "size INTEGER NOT NULL, blob TEXT NOT NULL, "
                           "PRIMARY KEY (message_luid, position))"),
            QStringLiteral("CREATE INDEX attachments_blob ON attachments (blob)"),
            // Messages pulled before these tables existed are only in
            // sync_objects; forgetting them makes the next pull load all.
            QStringLiteral("DELETE FROM sync_objects WHERE domain = 'msg'"),
            QStringLiteral("DELETE FROM meta WHERE key = 'counter.msg'"),
        },
        {
            QStringLiteral("CREATE TABLE calendars (url TEXT NOT NULL, title TEXT NOT NULL, "
                           "color INTEGER NOT NULL, hidden INTEGER NOT NULL, "
                           "timezone TEXT NOT NULL, access_level INTEGER NOT NULL, "
                           "selected INTEGER NOT NULL, is_default INTEGER NOT NULL, "
                           "PRIMARY KEY (url, title))"),
            QStringLiteral("CREATE TABLE events (luid INTEGER PRIMARY KEY NOT NULL, "
                           "calendar_url TEXT NOT NULL, calendar_title TEXT NOT NULL, "
                           "summary TEXT NOT NULL, location TEXT NOT NULL, "
                           "description TEXT NOT NULL, start_utc TEXT NOT NULL, "
                           "end_utc TEXT NOT NULL, all_day INTEGER NOT NULL, rrule TEXT NOT NULL, "
                           "exdates TEXT NOT NULL, alarms TEXT NOT NULL, class INTEGER NOT NULL, "
                           "transp INTEGER NOT NULL, color INTEGER, raw BLOB NOT NULL, "
                           "hash BLOB NOT NULL)"),
            QStringLiteral("CREATE INDEX events_start ON events (start_utc)"),
            QStringLiteral("CREATE TABLE tasks (luid INTEGER PRIMARY KEY NOT NULL, "
                           "summary TEXT NOT NULL, description TEXT NOT NULL, "
                           "start_date TEXT NOT NULL, due_date TEXT NOT NULL, "
                           "reminder_utc TEXT NOT NULL, rrule TEXT NOT NULL, "
                           "priority INTEGER NOT NULL, completed INTEGER NOT NULL, "
                           "categories TEXT NOT NULL, modified_utc TEXT NOT NULL, "
                           "raw BLOB NOT NULL, hash BLOB NOT NULL)"),
            QStringLiteral("CREATE TABLE notes (luid INTEGER PRIMARY KEY NOT NULL, "
                           "body TEXT NOT NULL, modified_utc TEXT NOT NULL, "
                           "categories TEXT NOT NULL, raw BLOB NOT NULL, hash BLOB NOT NULL)"),
            QStringLiteral("CREATE TABLE categories (domain TEXT NOT NULL, id INTEGER NOT NULL, "
                           "name TEXT NOT NULL, color INTEGER NOT NULL, PRIMARY KEY (domain, id))"),
            // Calendar and notes pulled before these tables existed are only
            // in sync_objects; forgetting them makes the next pull load all.
            QStringLiteral("DELETE FROM sync_objects WHERE domain IN ('cal', 'notes')"),
            QStringLiteral("DELETE FROM meta WHERE key IN ('counter.cal', 'counter.notes')"),
        },
    };
    return steps;
}

// Statements of a step that may fail without failing the step, because the
// SQLite of the system may lack a feature. Index as in migrations().
QStringList optionalMigrationStatements(int fromVersion)
{
    if (fromVersion == 3) {
        return {QStringLiteral("CREATE VIRTUAL TABLE messages_fts USING fts5(body, subject, "
                               "addresses, tokenize = 'unicode61 remove_diacritics 2')")};
    }
    return {};
}

QString counterKey(sync::IrmcDomain domain)
{
    return QStringLiteral("counter.") + sync::IrmcSync::areaName(domain);
}

QString lastSyncKey(sync::IrmcDomain domain)
{
    return QStringLiteral("lastSync.") + sync::IrmcSync::areaName(domain);
}

QString transportText(TransportKind kind)
{
    switch (kind) {
    case TransportKind::Tcp:
        return QStringLiteral("tcp");
    case TransportKind::Adb:
        return QStringLiteral("adb");
    case TransportKind::Bluetooth:
        return QStringLiteral("bluetooth");
    case TransportKind::None:
        break;
    }
    return QStringLiteral("none");
}

TransportKind transportFromText(const QString &text)
{
    if (text == QLatin1StringView("tcp"))
        return TransportKind::Tcp;
    if (text == QLatin1StringView("adb"))
        return TransportKind::Adb;
    if (text == QLatin1StringView("bluetooth"))
        return TransportKind::Bluetooth;
    return TransportKind::None;
}

bool isSha1Hex(const QString &name)
{
    if (name.size() != sha1HexLength)
        return false;
    for (const QChar c : name) {
        const bool digit = c >= u'0' && c <= u'9';
        const bool hexLetter = c >= u'a' && c <= u'f';
        if (!digit && !hexLetter)
            return false;
    }
    return true;
}

Error queryError(const QString &what, const QSqlQuery &query)
{
    return Error(ErrorCode::IoFailure, QStringLiteral("%1: %2").arg(what, query.lastError().text()));
}

std::optional<int> readUserVersion(const QSqlDatabase &db)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next())
        return std::nullopt;
    return query.value(0).toInt();
}

// Reads the "profile.*" rows. Missing fields keep their defaults.
Result<DeviceProfile> readProfile(const QSqlDatabase &db)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT key, value FROM meta WHERE key LIKE :prefix"));
    query.bindValue(QStringLiteral(":prefix"), profilePrefix() + u'%');
    if (!query.exec())
        return queryError(QStringLiteral("Cannot read the profile"), query);

    DeviceProfile profile;
    while (query.next()) {
        const QString key = query.value(0).toString().mid(profilePrefix().size());
        const QString value = query.value(1).toString();
        if (key == QLatin1StringView("deviceId")) {
            profile.deviceId = value;
        } else if (key == QLatin1StringView("displayName")) {
            profile.displayName = value;
        } else if (key == QLatin1StringView("model")) {
            profile.model = value;
        } else if (key == QLatin1StringView("manufacturer")) {
            profile.manufacturer = value;
        } else if (key == QLatin1StringView("androidApi")) {
            profile.androidApi = value.toInt();
        } else if (key == QLatin1StringView("clientVersion")) {
            profile.clientVersion = value;
        } else if (key == QLatin1StringView("lastConnection")) {
            profile.lastConnection = QDateTime::fromString(value, Qt::ISODateWithMs);
        } else if (key == QLatin1StringView("transport")) {
            profile.transport = transportFromText(value);
        } else if (key == QLatin1StringView("fixedAddress")) {
            profile.fixedAddress = value;
        } else if (key == QLatin1StringView("btAddress")) {
            profile.btAddress = value;
        } else if (key == QLatin1StringView("adbSerial")) {
            profile.adbSerial = value;
        } else if (key == QLatin1StringView("pinStored")) {
            profile.pinStored = value == QLatin1StringView("1");
        }
    }
    return profile;
}

QString newConnectionName()
{
    return QStringLiteral("qpm.profile.") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace

ProfileStore::ProfileStore(QString profilesDirectory, QString deviceId)
    : m_profilesDirectory(std::move(profilesDirectory)), m_deviceId(std::move(deviceId))
{
}

ProfileStore::~ProfileStore()
{
    if (m_connectionName.isEmpty())
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString ProfileStore::defaultProfilesDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/profiles");
}

QString ProfileStore::directoryName(const QString &deviceId)
{
    bool plain = !deviceId.isEmpty() && deviceId.size() <= maxDirectoryNameLength;
    for (const QChar c : deviceId) {
        const bool allowed = (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') ||
                             (c >= u'0' && c <= u'9') || c == u'-';
        if (!allowed) {
            plain = false;
            break;
        }
    }
    if (plain)
        return deviceId;
    return QString::fromLatin1(
        QCryptographicHash::hash(deviceId.toUtf8(), QCryptographicHash::Sha1).toHex());
}

Result<QList<DeviceProfile>> ProfileStore::listProfiles(const QString &profilesDirectory)
{
    if (!QSqlDatabase::isDriverAvailable(driverName()))
        return Error(ErrorCode::NotSupported, QStringLiteral("The SQLite driver is not available"));

    QList<DeviceProfile> profiles;
    const QDir root(profilesDirectory);
    const QStringList folders = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &folder : folders) {
        const QString path = root.filePath(folder + u'/' + databaseFile());
        if (!QFileInfo::exists(path))
            continue;

        const QString connectionName = newConnectionName();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(driverName(), connectionName);
            db.setDatabaseName(path);
            db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
            const std::optional<int> version = db.open() ? readUserVersion(db) : std::nullopt;
            if (!version || *version < 1 || *version > schemaVersion) {
                qCWarning(lcStore) << "Skipping a profile folder whose database cannot be read";
            } else {
                const Result<DeviceProfile> profile = readProfile(db);
                if (profile.hasValue() && !profile.value().deviceId.isEmpty())
                    profiles.append(profile.value());
                else
                    qCWarning(lcStore) << "Skipping a profile folder without a device id";
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connectionName);
    }
    return profiles;
}

Result<void> ProfileStore::removeProfile(const QString &profilesDirectory,
                                         const QString &deviceId)
{
    if (deviceId.isEmpty())
        return Error(ErrorCode::InvalidInput, QStringLiteral("Empty device id"));
    const QFileInfo folder(QDir(profilesDirectory).filePath(directoryName(deviceId)));
    if (!folder.exists())
        return {};
    const QString base = QFileInfo(profilesDirectory).canonicalFilePath();
    const QString target = folder.canonicalFilePath();
    // A folder that is a link to somewhere else must not take that place
    // with it.
    if (base.isEmpty() || folder.isSymLink() || !target.startsWith(base + u'/')) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("The profile folder %1 is not below the profiles folder")
                         .arg(folder.fileName()));
    }
    if (!QDir(target).removeRecursively()) {
        return Error(ErrorCode::IoFailure,
                     QStringLiteral("Could not delete the profile folder %1").arg(folder.fileName()));
    }
    return {};
}

Result<void> ProfileStore::open()
{
    if (m_open)
        return {};
    if (m_deviceId.isEmpty())
        return Error(ErrorCode::InvalidInput, QStringLiteral("The device id is empty"));
    if (!QSqlDatabase::isDriverAvailable(driverName()))
        return Error(ErrorCode::NotSupported, QStringLiteral("The SQLite driver is not available"));

    const QDir folder(directory());
    if (!QDir().mkpath(folder.filePath(blobsFolder())))
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot create the profile folder"));

    const QString databasePath = folder.filePath(databaseFile());
    const bool existed = QFileInfo(databasePath).size() > 0;

    if (m_connectionName.isEmpty())
        m_connectionName = newConnectionName();
    QSqlDatabase db = QSqlDatabase::contains(m_connectionName)
                          ? QSqlDatabase::database(m_connectionName, false)
                          : QSqlDatabase::addDatabase(driverName(), m_connectionName);
    db.setDatabaseName(databasePath);
    if (!db.open()) {
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot open the profile database: %1")
                                               .arg(db.lastError().text()));
    }

    const std::optional<int> version = readUserVersion(db);
    if (!version) {
        db.close();
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot read the schema version"));
    }
    if (*version > schemaVersion) {
        db.close();
        return Error(ErrorCode::NotSupported,
                     QStringLiteral("The profile database has schema version %1, this program "
                                    "knows up to %2")
                         .arg(*version)
                         .arg(schemaVersion));
    }

    if (*version < schemaVersion) {
        if (existed) {
            const QString backupPath = folder.filePath(backupFile());
            if (QFile::exists(backupPath) && !QFile::remove(backupPath)) {
                db.close();
                return Error(ErrorCode::IoFailure, QStringLiteral("Cannot replace the old backup"));
            }
            // No transaction is open at this point, so the file on disk is
            // complete and can be copied while the connection stays open.
            if (!QFile::copy(databasePath, backupPath)) {
                db.close();
                return Error(ErrorCode::IoFailure,
                             QStringLiteral("Cannot back up the database before the migration"));
            }
        }
        const Result<void> migrated = migrate(*version);
        if (!migrated.hasValue()) {
            db.close();
            return migrated;
        }
    }

    m_open = true;
    return {};
}

Result<void> ProfileStore::migrate(int fromVersion)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    for (int version = fromVersion; version < schemaVersion; ++version) {
        if (!db.transaction())
            return Error(ErrorCode::IoFailure, QStringLiteral("Cannot start the migration"));

        QSqlQuery query(db);
        bool ok = true;
        for (const QString &statement : migrations().at(version)) {
            if (!query.exec(statement)) {
                ok = false;
                break;
            }
        }
        if (ok) {
            const QStringList optional = optionalMigrationStatements(version);
            for (const QString &statement : optional) {
                // A failed statement leaves the transaction open in SQLite;
                // the step goes on without the feature.
                if (!query.exec(statement)) {
                    qCInfo(lcStore) << "Optional part of the migration left out:"
                                    << query.lastError().text();
                }
            }
        }
        // SQLite takes no bound value in a PRAGMA, so the version is written
        // into the statement. It is an integer from this file, never input.
        if (ok)
            ok = query.exec(QStringLiteral("PRAGMA user_version = %1").arg(version + 1));

        if (!ok) {
            const Error error =
                queryError(QStringLiteral("Migration to version %1 failed").arg(version + 1), query);
            db.rollback();
            return error;
        }
        if (!db.commit())
            return Error(ErrorCode::IoFailure, QStringLiteral("Cannot commit the migration"));
        qCInfo(lcStore) << "Profile database migrated to schema version" << version + 1;
    }
    return {};
}

QString ProfileStore::deviceId() const
{
    return m_deviceId;
}

QString ProfileStore::directory() const
{
    return QDir(m_profilesDirectory).filePath(directoryName(m_deviceId));
}

Result<DeviceProfile> ProfileStore::loadProfile() const
{
    if (!m_open)
        return Error(ErrorCode::IoFailure, QStringLiteral("The profile store is not open"));
    Result<DeviceProfile> loaded = readProfile(QSqlDatabase::database(m_connectionName, false));
    if (!loaded.hasValue())
        return loaded;
    DeviceProfile profile = loaded.value();
    profile.deviceId = m_deviceId;
    return profile;
}

Result<void> ProfileStore::saveProfile(const DeviceProfile &profile)
{
    if (!m_open)
        return Error(ErrorCode::IoFailure, QStringLiteral("The profile store is not open"));
    if (profile.deviceId != m_deviceId)
        return Error(ErrorCode::InvalidInput, QStringLiteral("The profile belongs to another device"));
    if (profile.displayName.isEmpty() || profile.displayName.size() > maxDisplayNameLength) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("The display name has %1 characters, allowed are 1 to %2")
                         .arg(profile.displayName.size())
                         .arg(maxDisplayNameLength));
    }

    const QString lastConnection = profile.lastConnection.isValid()
                                       ? profile.lastConnection.toUTC().toString(Qt::ISODateWithMs)
                                       : QString();
    return writeMeta({
        {profilePrefix() + QStringLiteral("deviceId"), profile.deviceId},
        {profilePrefix() + QStringLiteral("displayName"), profile.displayName},
        {profilePrefix() + QStringLiteral("model"), profile.model},
        {profilePrefix() + QStringLiteral("manufacturer"), profile.manufacturer},
        {profilePrefix() + QStringLiteral("androidApi"), QString::number(profile.androidApi)},
        {profilePrefix() + QStringLiteral("clientVersion"), profile.clientVersion},
        {profilePrefix() + QStringLiteral("lastConnection"), lastConnection},
        {profilePrefix() + QStringLiteral("transport"), transportText(profile.transport)},
        {profilePrefix() + QStringLiteral("fixedAddress"), profile.fixedAddress},
        {profilePrefix() + QStringLiteral("btAddress"), profile.btAddress},
        {profilePrefix() + QStringLiteral("adbSerial"), profile.adbSerial},
        {profilePrefix() + QStringLiteral("pinStored"),
         profile.pinStored ? QStringLiteral("1") : QStringLiteral("0")},
    });
}

Result<QString> ProfileStore::putBlob(const QByteArray &data)
{
    if (!m_open)
        return Error(ErrorCode::IoFailure, QStringLiteral("The profile store is not open"));
    return putBlobIn(directory(), data);
}

Result<QString> ProfileStore::putBlobIn(const QString &profileDirectory, const QByteArray &data)
{
    const QString name =
        QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
    const QString path = QDir(profileDirectory).filePath(blobsFolder() + u'/' + name);
    if (QFileInfo::exists(path))
        return name;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot write blob %1").arg(name));
    return name;
}

Result<QByteArray> ProfileStore::blob(const QString &name) const
{
    if (!m_open)
        return Error(ErrorCode::IoFailure, QStringLiteral("The profile store is not open"));
    if (!isSha1Hex(name))
        return Error(ErrorCode::InvalidInput, QStringLiteral("Not a blob name: %1").arg(name.left(64)));

    QFile file(QDir(directory()).filePath(blobsFolder() + u'/' + name));
    if (!file.exists())
        return Error(ErrorCode::NotFound, QStringLiteral("No blob %1").arg(name));
    if (!file.open(QIODevice::ReadOnly))
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot read blob %1").arg(name));
    return file.readAll();
}

Result<void> ProfileStore::removeBlob(const QString &name)
{
    if (!m_open)
        return Error(ErrorCode::IoFailure, QStringLiteral("The profile store is not open"));
    if (!isSha1Hex(name)) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("Not a blob name: %1").arg(name.left(64)));
    }

    QFile file(QDir(directory()).filePath(blobsFolder() + u'/' + name));
    if (file.exists() && !file.remove())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot delete blob %1").arg(name));
    return {};
}

std::optional<QDateTime> ProfileStore::lastSync(sync::IrmcDomain domain) const
{
    const std::optional<QString> value = metaValue(lastSyncKey(domain));
    if (!value)
        return std::nullopt;
    const QDateTime time = QDateTime::fromString(*value, Qt::ISODateWithMs);
    return time.isValid() ? std::optional<QDateTime>(time) : std::nullopt;
}

std::optional<quint64> ProfileStore::changeCounter(sync::IrmcDomain domain) const
{
    const std::optional<QString> value = metaValue(counterKey(domain));
    if (!value)
        return std::nullopt;
    bool ok = false;
    const quint64 counter = value->toULongLong(&ok);
    return ok ? std::optional<quint64>(counter) : std::nullopt;
}

std::optional<QByteArray> ProfileStore::objectHash(sync::IrmcDomain domain, quint32 luid) const
{
    if (!m_open)
        return std::nullopt;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(
        QStringLiteral("SELECT hash FROM sync_objects WHERE domain = :domain AND luid = :luid"));
    query.bindValue(QStringLiteral(":domain"), sync::IrmcSync::areaName(domain));
    query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
    if (!query.exec()) {
        // The adapter interface has no error channel; an unknown hash makes
        // the sync fetch the object again, which is safe.
        qCWarning(lcStore) << "Cannot read an object hash:" << query.lastError().text();
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;
    return query.value(0).toByteArray();
}

Result<void> ProfileStore::apply(sync::IrmcDomain domain, const sync::SyncDelta &delta)
{
    return applyWith(domain, delta, {});
}

Result<void> ProfileStore::applyWith(sync::IrmcDomain domain, const sync::SyncDelta &delta,
                                     const ExtraStep &extra)
{
    if (!m_open)
        return Error(ErrorCode::IoFailure, QStringLiteral("The profile store is not open"));

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.transaction())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot start a transaction"));

    const QString area = sync::IrmcSync::areaName(domain);
    const auto fail = [&db](const Error &error) -> Result<void> {
        db.rollback();
        return error;
    };

    QSqlQuery query(db);
    if (delta.full) {
        query.prepare(QStringLiteral("DELETE FROM sync_objects WHERE domain = :domain"));
        query.bindValue(QStringLiteral(":domain"), area);
        if (!query.exec())
            return fail(queryError(QStringLiteral("Cannot clear the area"), query));
    }

    query.prepare(QStringLiteral("INSERT OR REPLACE INTO sync_objects (domain, luid, hash, raw) "
                                 "VALUES (:domain, :luid, :hash, :raw)"));
    for (const sync::RawObject &object : delta.upserts) {
        const QByteArray hash = object.hash.isEmpty()
                                    ? QCryptographicHash::hash(object.data, QCryptographicHash::Sha1)
                                    : object.hash;
        query.bindValue(QStringLiteral(":domain"), area);
        query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(object.luid));
        query.bindValue(QStringLiteral(":hash"), hash);
        query.bindValue(QStringLiteral(":raw"), object.data);
        if (!query.exec())
            return fail(queryError(QStringLiteral("Cannot store an object"), query));
    }

    query.prepare(QStringLiteral("DELETE FROM sync_objects WHERE domain = :domain AND luid = :luid"));
    for (const quint32 luid : delta.deletes) {
        query.bindValue(QStringLiteral(":domain"), area);
        query.bindValue(QStringLiteral(":luid"), static_cast<qint64>(luid));
        if (!query.exec())
            return fail(queryError(QStringLiteral("Cannot delete an object"), query));
    }

    query.prepare(QStringLiteral("INSERT OR REPLACE INTO meta (key, value) VALUES (:key, :value)"));
    const QList<std::pair<QString, QString>> values = {
        {counterKey(domain), QString::number(delta.newCounter)},
        {lastSyncKey(domain), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
    };
    for (const auto &[key, value] : values) {
        query.bindValue(QStringLiteral(":key"), key);
        query.bindValue(QStringLiteral(":value"), value);
        if (!query.exec())
            return fail(queryError(QStringLiteral("Cannot store the change counter"), query));
    }

    if (extra) {
        const Result<void> done = extra(m_connectionName);
        if (!done.hasValue())
            return fail(done.error());
    }

    if (!db.commit())
        return fail(Error(ErrorCode::IoFailure, QStringLiteral("Cannot commit the changes")));
    return {};
}

std::optional<QString> ProfileStore::metaValue(const QString &key) const
{
    if (!m_open)
        return std::nullopt;
    QSqlQuery query(QSqlDatabase::database(m_connectionName, false));
    query.prepare(QStringLiteral("SELECT value FROM meta WHERE key = :key"));
    query.bindValue(QStringLiteral(":key"), key);
    if (!query.exec()) {
        // Callers treat a missing value as "never stored", which is the safe
        // reading for counters and times.
        qCWarning(lcStore) << "Cannot read a stored value:" << query.lastError().text();
        return std::nullopt;
    }
    if (!query.next())
        return std::nullopt;
    return query.value(0).toString();
}

Result<void> ProfileStore::writeMeta(const QList<std::pair<QString, QString>> &values)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!db.transaction())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot start a transaction"));

    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO meta (key, value) VALUES (:key, :value)"));
    for (const auto &[key, value] : values) {
        query.bindValue(QStringLiteral(":key"), key);
        query.bindValue(QStringLiteral(":value"), value);
        if (!query.exec()) {
            const Error error = queryError(QStringLiteral("Cannot store the profile"), query);
            db.rollback();
            return error;
        }
    }
    if (!db.commit())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot commit the profile"));
    return {};
}

} // namespace qpm::core::store
