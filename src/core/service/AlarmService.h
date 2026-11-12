#pragma once

#include "core/Error.h"

#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>
#include <QTime>

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::service {

/// One alarm of the clock app on the phone.
struct PhoneAlarm
{
    /// Row of the clock app; 0 creates a new alarm.
    int id = 0;
    QTime time;
    QString name;
    /// 1 Monday to 7 Sunday; empty for an alarm that rings once.
    QList<int> days;
    bool enabled = true;

    bool operator==(const PhoneAlarm &other) const = default;
};

/**
 * The alarms of the clock app on the phone through AT+CALA. Many phones keep
 * their alarms where the client cannot reach them; the client then answers
 * with an error, which arrives here as ErrorCode::NotSupported.
 *
 * Lives in the main thread; no call blocks. Does not own the connection,
 * which must outlive it. Every call fails with ErrorCode::ConnectionClosed
 * when the phone is not connected.
 */
class AlarmService : public QObject
{
    Q_OBJECT

public:
    explicit AlarmService(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /// The alarms in the order of the phone.
    QFuture<Result<QList<PhoneAlarm>>> alarms();

    /**
     * Creates the alarm when its id is 0, otherwise changes it. Fails with
     * ErrorCode::InvalidInput for an invalid time or a day outside 1 to 7,
     * and with ErrorCode::NotSupported when the phone refuses.
     */
    QFuture<Result<void>> save(const PhoneAlarm &alarm);

    /// Reads the lines of an answer to AT+CALA?; lines that cannot be read are skipped.
    [[nodiscard]] static QList<PhoneAlarm> parseAlarms(const QStringList &lines);

    /// The command that saves `alarm`.
    [[nodiscard]] static QString saveCommand(const PhoneAlarm &alarm);

private:
    protocol::PhoneConnection *m_connection = nullptr;
};

} // namespace qpm::core::service
