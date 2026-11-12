#include "core/service/AlarmService.h"

#include "core/codec/AtText.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/PhoneConnection.h"

#include <algorithm>

namespace qpm::core::service {

namespace {

using protocol::AtReply;

constexpr int daysPerWeek = 7;
constexpr int fieldCount = 6;
constexpr qsizetype maxNameLength = 100;

QString unquoted(const QString &field)
{
    const QString trimmed = field.trimmed();
    if (trimmed.size() >= 2 && trimmed.startsWith(u'"') && trimmed.endsWith(u'"'))
        return trimmed.mid(1, trimmed.size() - 2);
    return trimmed;
}

Error replyError(const AtReply &reply, const QString &what)
{
    switch (reply.status) {
    case AtReply::Status::Ok:
        break;
    case AtReply::Status::Timeout:
        return Error(ErrorCode::Timeout, QStringLiteral("The phone did not answer %1").arg(what));
    case AtReply::Status::Closed:
        return Error(ErrorCode::ConnectionClosed, QStringLiteral("The phone is not connected"));
    case AtReply::Status::PermissionDenied:
        return Error(ErrorCode::PermissionDenied,
                     QStringLiteral("The phone app may not reach the alarms"));
    default:
        return Error(ErrorCode::NotSupported,
                     QStringLiteral("The phone offers no access to its alarms"));
    }
    return {};
}

template <typename T> QFuture<Result<T>> failed(const Error &error)
{
    return QtFuture::makeReadyValueFuture(Result<T>(error));
}

} // namespace

AlarmService::AlarmService(protocol::PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    Q_ASSERT(connection);
}

QList<PhoneAlarm> AlarmService::parseAlarms(const QStringList &lines)
{
    static constexpr QLatin1StringView prefix("+CALA:");
    QList<PhoneAlarm> alarms;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(prefix))
            continue;
        const QStringList fields = codec::splitFields(trimmed.mid(prefix.size()).trimmed());
        if (fields.size() < fieldCount)
            continue;
        PhoneAlarm alarm;
        alarm.time = QTime::fromString(unquoted(fields.at(0)), QStringLiteral("HH:mm"));
        bool ok = false;
        alarm.id = fields.at(1).trimmed().toInt(&ok);
        if (!alarm.time.isValid() || !ok || alarm.id <= 0)
            continue;
        const bool once = fields.at(2).trimmed() == QLatin1StringView("1");
        alarm.name = codec::decodeQp(unquoted(fields.at(3)));
        // The day list is quoted and may itself hold commas.
        const QString days = unquoted(fields.mid(4, fields.size() - 5).join(u','));
        if (!once) {
            if (days == QLatin1StringView("0")) {
                for (int day = 1; day <= daysPerWeek; ++day)
                    alarm.days.append(day);
            } else {
                const QStringList parts = days.split(u',', Qt::SkipEmptyParts);
                for (const QString &part : parts) {
                    const int day = part.trimmed().toInt(&ok);
                    if (ok && day >= 1 && day <= daysPerWeek && !alarm.days.contains(day))
                        alarm.days.append(day);
                }
                std::sort(alarm.days.begin(), alarm.days.end());
            }
        }
        alarm.enabled = fields.constLast().trimmed() != QLatin1StringView("0");
        alarms.append(alarm);
    }
    return alarms;
}

QString AlarmService::saveCommand(const PhoneAlarm &alarm)
{
    QString command = QStringLiteral("AT+CALA=\"%1\",%2,%3,\"%4\"")
                          .arg(alarm.time.toString(QStringLiteral("HH:mm")))
                          .arg(alarm.id)
                          .arg(alarm.days.isEmpty() ? 1 : 0)
                          .arg(codec::encodeQp(alarm.name.left(maxNameLength)));
    if (!alarm.days.isEmpty()) {
        QStringList days;
        for (const int day : alarm.days)
            days.append(QString::number(day));
        command += QStringLiteral(",\"%1\"").arg(days.join(u','));
    }
    command += alarm.enabled ? QStringLiteral(",1") : QStringLiteral(",0");
    return command;
}

QFuture<Result<QList<PhoneAlarm>>> AlarmService::alarms()
{
    protocol::AtSession *session = m_connection->session();
    if (!session) {
        return failed<QList<PhoneAlarm>>(
            Error(ErrorCode::ConnectionClosed, QStringLiteral("The phone is not connected")));
    }
    return session->send(QStringLiteral("AT+CALA?"))
        .then(this, [](const AtReply &reply) -> Result<QList<PhoneAlarm>> {
            const Error error = replyError(reply, QStringLiteral("AT+CALA?"));
            if (error.isError())
                return error;
            return parseAlarms(reply.lines);
        });
}

QFuture<Result<void>> AlarmService::save(const PhoneAlarm &alarm)
{
    if (!alarm.time.isValid() || alarm.id < 0) {
        return failed<void>(
            Error(ErrorCode::InvalidInput, QStringLiteral("An alarm needs a valid time")));
    }
    for (const int day : alarm.days) {
        if (day < 1 || day > daysPerWeek) {
            return failed<void>(
                Error(ErrorCode::InvalidInput, QStringLiteral("Day %1 is not 1 to 7").arg(day)));
        }
    }
    protocol::AtSession *session = m_connection->session();
    if (!session) {
        return failed<void>(
            Error(ErrorCode::ConnectionClosed, QStringLiteral("The phone is not connected")));
    }
    return session->send(saveCommand(alarm)).then(this, [](const AtReply &reply) -> Result<void> {
        const Error error = replyError(reply, QStringLiteral("AT+CALA="));
        if (error.isError())
            return error;
        return {};
    });
}

} // namespace qpm::core::service
