#pragma once

#include "core/protocol/PhoneConnection.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace qpm::core::service {

/// The operator of the SIM card, as the phone reports it.
struct NetworkOperator
{
    QString name;
    /// Mobile country and network code, for example "26201".
    QString mccMnc;
    /// ISO country code in lower case, for example "de".
    QString country;
};

/// One active SIM subscription. The number is personal data and never logged.
struct SimCard
{
    int slot = 0;
    QString number;
    QString name;
    QString carrier;
    bool defaultForCalls = false;
    bool defaultForMessages = false;
};

/// Size and free space of one storage root of the phone.
struct StorageSpace
{
    /// "sdcard" (internal shared storage), "external" (memory card) or "data".
    QString root;
    quint64 totalBytes = 0;
    quint64 freeBytes = 0;
};

struct DisplayInfo
{
    int width = 0;
    int height = 0;
    int densityDpi = 0;
};

/// Everything the overview shows about the connected phone.
struct DeviceOverview
{
    protocol::DeviceIdentity identity;
    protocol::DeviceStatus status;
    std::optional<NetworkOperator> networkOperator;
    QList<SimCard> sims;
    /// The phone answered the SIM query; without that the list means nothing.
    bool simsKnown = false;
    /// The phone app lacks the phone permission and cannot list the SIM cards.
    bool simPermissionMissing = false;
    QList<StorageSpace> storage;
    std::optional<DisplayInfo> display;
    /// Build number of the phone software.
    QString firmware;
};

/**
 * Collects the state of the connected phone for the overview: identity,
 * battery, signal and processor load from the connection, and operator, SIM
 * cards, storage, display and firmware through queries of its own once the
 * phone is ready. After every OBEX session the storage is asked for again,
 * since a transfer changes it and the phone reports nothing while OBEX runs.
 *
 * Every value from the phone is checked against a range or a length; a value
 * outside stays unknown. The overview is cleared when the connection ends.
 * Does not own the connection, which must outlive it. Main thread only.
 */
class DeviceStatusService : public QObject
{
    Q_OBJECT

public:
    explicit DeviceStatusService(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    [[nodiscard]] const DeviceOverview &overview() const { return m_overview; }

    /// True once the phone has identified itself in the current connection.
    [[nodiscard]] bool hasData() const { return !m_overview.identity.deviceId.isEmpty(); }

    /// Asks the phone for every value again; ignored while it is not ready.
    void refresh();

    /// Parses the reply lines of AT*SOPS; nothing when the name is empty.
    [[nodiscard]] static std::optional<NetworkOperator> parseOperator(const QStringList &lines);
    /// Parses the reply lines of AT*SIM; lines that do not parse are skipped.
    [[nodiscard]] static QList<SimCard> parseSims(const QStringList &lines);
    /// Parses the reply lines of AT*STATFS.
    [[nodiscard]] static QList<StorageSpace> parseStorage(const QStringList &lines);
    /// Parses the reply lines of AT*DISPLAY.
    [[nodiscard]] static std::optional<DisplayInfo> parseDisplay(const QStringList &lines);

Q_SIGNALS:
    void changed();

private:
    void onStateChanged(protocol::PhoneConnection::State state);
    void query(const QString &command);
    void handleReply(const protocol::AtReply &reply);

    protocol::PhoneConnection *m_connection = nullptr;
    protocol::PhoneConnection::State m_lastState = protocol::PhoneConnection::State::Disconnected;
    DeviceOverview m_overview;
    quint64 m_generation = 0;
};

} // namespace qpm::core::service
