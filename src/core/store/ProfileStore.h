#pragma once

#include "core/Error.h"
#include "core/sync/SyncStoreAdapter.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <functional>
#include <optional>
#include <utility>

namespace qpm::core::store {

/// How the computer last reached the phone.
enum class TransportKind { None, Tcp, Adb, Bluetooth };

/// What the computer keeps about one phone besides its data.
struct DeviceProfile
{
    /// Answer to AT+CGSN; never empty.
    QString deviceId;
    /// 1 to 64 characters.
    QString displayName;
    QString model;
    QString manufacturer;
    int androidApi = 0;
    QString clientVersion;
    /// UTC; invalid before the first connection.
    QDateTime lastConnection;
    TransportKind transport = TransportKind::None;
    QString fixedAddress;
    QString btAddress;
    QString adbSerial;
    /// Whether the Secret Service holds a PIN for this phone. The PIN itself
    /// is never stored here.
    bool pinStored = false;
};

/**
 * The local store of one phone: the folder `<profiles>/<directoryName>/` with
 * the database `data.sqlite` and the folder `blobs/`.
 *
 * open() creates what is missing and brings the schema to the current
 * version, one step at a time. Before an existing database is migrated, it is
 * copied to `data.sqlite.bak`. A database written by a newer version is not
 * touched.
 *
 * Every call other than open() and openReadOnly() requires one of them to
 * have succeeded. Use from the thread that opened it only; the object owns
 * its database connection and closes it on destruction. The stores of the
 * program use theirs from the main thread.
 */
class ProfileStore : public sync::SyncStoreAdapter
{
public:
    /// The version open() migrates to.
    static constexpr int schemaVersion = 5;

    /// Work that runs inside the transaction of applyWith(), on the store's connection.
    using ExtraStep = std::function<Result<void>(const QString &connectionName)>;

    /**
     * `profilesDirectory` is the folder that holds one subfolder per phone,
     * see defaultProfilesDirectory().
     */
    ProfileStore(QString profilesDirectory, QString deviceId);
    ~ProfileStore() override;

    ProfileStore(const ProfileStore &) = delete;
    ProfileStore &operator=(const ProfileStore &) = delete;
    ProfileStore(ProfileStore &&) = delete;
    ProfileStore &operator=(ProfileStore &&) = delete;

    /// `profiles` below the application data folder of the current user.
    [[nodiscard]] static QString defaultProfilesDirectory();

    /**
     * The folder name for a device id: the id itself when it consists of
     * ASCII letters, digits and hyphens only and has at most 64 characters,
     * otherwise the hex SHA-1 of the id. The result never leaves the folder
     * it is appended to.
     */
    [[nodiscard]] static QString directoryName(const QString &deviceId);

    /**
     * The profiles found below `profilesDirectory`, in no particular order.
     * A folder whose database cannot be read is skipped and logged.
     */
    [[nodiscard]] static Result<QList<DeviceProfile>> listProfiles(const QString &profilesDirectory);

    /**
     * Deletes the folder of the phone `deviceId` below `profilesDirectory`
     * with all its data. Succeeds when there is no such folder. A store of
     * that phone must not be open. Fails with ErrorCode::InvalidInput for an
     * empty device id or a folder that resolves outside `profilesDirectory`,
     * and with ErrorCode::IoFailure when not everything could be deleted.
     */
    [[nodiscard]] static Result<void> removeProfile(const QString &profilesDirectory,
                                                    const QString &deviceId);

    /**
     * Creates the folders and the database if they are missing and migrates
     * the schema. Fails with ErrorCode::InvalidInput for an empty device id,
     * ErrorCode::NotSupported when the database is newer than this program or
     * SQLite is not available, and ErrorCode::IoFailure otherwise.
     */
    [[nodiscard]] Result<void> open();

    /**
     * Opens the database of an existing profile for reading only, so that a
     * worker thread can read while the main thread keeps its own connection;
     * a read waits up to ten seconds for a write of that connection. Creates
     * and migrates nothing. Fails with ErrorCode::InvalidInput for an empty
     * device id, ErrorCode::NotFound when the profile has no database,
     * ErrorCode::NotSupported when its schema version is not schemaVersion,
     * and ErrorCode::IoFailure otherwise.
     */
    [[nodiscard]] Result<void> openReadOnly();

    [[nodiscard]] QString deviceId() const;
    /// The profile folder.
    [[nodiscard]] QString directory() const;

    /**
     * The stored profile. Fields that were never saved keep their defaults;
     * deviceId is always the id of this store.
     */
    [[nodiscard]] Result<DeviceProfile> loadProfile() const;

    /**
     * Stores the profile fields. Fails with ErrorCode::InvalidInput when the
     * device id differs from this store or the display name is not 1 to 64
     * characters long.
     */
    [[nodiscard]] Result<void> saveProfile(const DeviceProfile &profile);

    /**
     * Writes `data` to `blobs/<sha1>` unless that file exists and returns the
     * hex SHA-1, which names the blob from then on.
     */
    [[nodiscard]] Result<QString> putBlob(const QByteArray &data);

    /**
     * Like putBlob() for the profile folder `profileDirectory`, see directory(),
     * without touching the database, so that a worker thread may call it. Two
     * calls with the same data at the same time leave one complete file.
     */
    [[nodiscard]] static Result<QString> putBlobIn(const QString &profileDirectory,
                                                   const QByteArray &data);

    /**
     * Reads a blob. Fails with ErrorCode::InvalidInput when `name` is not a
     * hex SHA-1 and ErrorCode::NotFound when no such blob exists.
     */
    [[nodiscard]] Result<QByteArray> blob(const QString &name) const;

    /**
     * Deletes the blob `name`; succeeds when there is no such blob. Fails with
     * ErrorCode::InvalidInput when `name` is not a hex SHA-1 and
     * ErrorCode::IoFailure when the file cannot be deleted. Whether anything
     * still refers to the blob is for the caller to know.
     */
    [[nodiscard]] Result<void> removeBlob(const QString &name);

    /// When the area was last pulled, or nothing before the first pull.
    [[nodiscard]] std::optional<QDateTime> lastSync(sync::IrmcDomain domain) const;

    // A failed read of the database is logged and reported as "nothing
    // stored", which makes the next pull load the area in full.
    [[nodiscard]] std::optional<quint64> changeCounter(sync::IrmcDomain domain) const override;
    [[nodiscard]] std::optional<QByteArray> objectHash(sync::IrmcDomain domain,
                                                       quint32 luid) const override;
    /// Also records the time of the pull, see lastSync().
    [[nodiscard]] Result<void> apply(sync::IrmcDomain domain, const sync::SyncDelta &delta) override;

    /**
     * Like apply(), and runs `extra` in the same transaction before it is
     * committed; when `extra` fails, nothing of the delta is kept.
     */
    [[nodiscard]] Result<void> applyWith(sync::IrmcDomain domain, const sync::SyncDelta &delta,
                                         const ExtraStep &extra);

    /**
     * Name of the database connection, for tables of the areas that keep their
     * own rows next to the sync state. Empty before open(). Use from the main
     * thread only, like the store.
     */
    [[nodiscard]] QString connectionName() const { return m_open ? m_connectionName : QString(); }

private:
    [[nodiscard]] Result<void> migrate(int fromVersion);
    [[nodiscard]] std::optional<QString> metaValue(const QString &key) const;
    [[nodiscard]] Result<void> writeMeta(const QList<std::pair<QString, QString>> &values);

    QString m_profilesDirectory;
    QString m_deviceId;
    QString m_connectionName;
    bool m_open = false;
};

} // namespace qpm::core::store
