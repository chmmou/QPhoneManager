#pragma once

#include "core/Error.h"
#include "core/sync/SyncStoreAdapter.h"

#include <QDateTime>
#include <QList>
#include <QString>

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
 * Every call other than open() requires a successful open(). Use from the
 * main thread only; the object owns its database connection and closes it on
 * destruction.
 */
class ProfileStore : public sync::SyncStoreAdapter
{
public:
    /// The version open() migrates to.
    static constexpr int schemaVersion = 1;

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
     * Creates the folders and the database if they are missing and migrates
     * the schema. Fails with ErrorCode::InvalidInput for an empty device id,
     * ErrorCode::NotSupported when the database is newer than this program or
     * SQLite is not available, and ErrorCode::IoFailure otherwise.
     */
    [[nodiscard]] Result<void> open();

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
     * Reads a blob. Fails with ErrorCode::InvalidInput when `name` is not a
     * hex SHA-1 and ErrorCode::NotFound when no such blob exists.
     */
    [[nodiscard]] Result<QByteArray> blob(const QString &name) const;

    /// When the area was last pulled, or nothing before the first pull.
    [[nodiscard]] std::optional<QDateTime> lastSync(sync::IrmcDomain domain) const;

    // A failed read of the database is logged and reported as "nothing
    // stored", which makes the next pull load the area in full.
    [[nodiscard]] std::optional<quint64> changeCounter(sync::IrmcDomain domain) const override;
    [[nodiscard]] std::optional<QByteArray> objectHash(sync::IrmcDomain domain,
                                                       quint32 luid) const override;
    /// Also records the time of the pull, see lastSync().
    [[nodiscard]] Result<void> apply(sync::IrmcDomain domain, const sync::SyncDelta &delta) override;

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
