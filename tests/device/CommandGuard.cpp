#include "CommandGuard.h"

#include "core/protocol/ObexPacket.h"

#include <QLoggingCategory>

namespace qpm::test {

namespace {
Q_LOGGING_CATEGORY(lcGuard, "qpm.test.guard")
}

using core::Error;
using core::ErrorCode;
using core::protocol::ObexPacket;
using core::protocol::Transport;

CommandGuard::CommandGuard(std::unique_ptr<Transport> inner, QObject *parent)
    : Transport(parent), m_inner(std::move(inner))
{
    Q_ASSERT(m_inner);
    connect(m_inner.get(), &Transport::opened, this, &Transport::opened);
    connect(m_inner.get(), &Transport::dataReceived, this, &Transport::dataReceived);
    connect(m_inner.get(), &Transport::failed, this, &Transport::failed);
    connect(m_inner.get(), &Transport::closed, this, &Transport::closed);
}

void CommandGuard::open()
{
    m_inner->open();
}

void CommandGuard::close()
{
    m_inner->close();
}

void CommandGuard::write(const QByteArray &frame)
{
    const std::optional<QString> reason = blockReasonForFrame(frame);
    if (!reason) {
        m_inner->write(frame);
        return;
    }
    qCWarning(lcGuard) << "Refused to send:" << *reason;
    m_blocked.append(*reason);
    Q_EMIT failed(
        Error(ErrorCode::Rejected, QStringLiteral("Test guard refused: %1").arg(*reason)));
    m_inner->close();
}

bool CommandGuard::isOpen() const
{
    return m_inner->isOpen();
}

QString CommandGuard::peerDescription() const
{
    return m_inner->peerDescription();
}

std::optional<QString> CommandGuard::blockReason(const QString &command)
{
    const QString upper = command.trimmed().toUpper();
    if (upper.startsWith(QLatin1StringView("ATD")))
        return QStringLiteral("ATD places a call");
    if (upper.startsWith(QLatin1StringView("ATA")))
        return QStringLiteral("ATA answers a call");
    if (upper.startsWith(QLatin1StringView("ATH")))
        return QStringLiteral("ATH ends a call");
    if (upper.startsWith(QLatin1StringView("AT+CHUP")))
        return QStringLiteral("AT+CHUP ends a call");
    if (upper.startsWith(QLatin1StringView("AT+CMGS=")) && upper != QLatin1StringView("AT+CMGS=?"))
        return QStringLiteral("AT+CMGS sends a text message");
    if (upper.startsWith(QLatin1StringView("AT*NOTIACTION")))
        return QStringLiteral("AT*NOTIACTION can reply to a message or call back");
    return std::nullopt;
}

std::optional<QString> CommandGuard::blockReasonForFrame(const QByteArray &frame)
{
    if (frame.startsWith("AT") || frame.startsWith("at"))
        return blockReason(QString::fromUtf8(frame));

    const quint8 code = frame.isEmpty() ? 0 : quint8(frame.at(0));
    if (code != core::protocol::obex::opPut && code != core::protocol::obex::opPutFinal)
        return std::nullopt;
    const auto packet = ObexPacket::decode(frame, ObexPacket::Layout::Plain);
    if (!packet.hasValue() || !packet.value().name)
        return std::nullopt;
    const QString name = packet.value().name->toLower();
    if (name.startsWith(QLatin1StringView("telecom/msg/")) &&
        name != QLatin1StringView("telecom/msg/setflags")) {
        return QStringLiteral("OBEX PUT below telecom/msg/ schedules a text message");
    }
    return std::nullopt;
}

} // namespace qpm::test
