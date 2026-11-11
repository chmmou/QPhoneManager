#include "CommandGuard.h"

#include "core/protocol/ObexPacket.h"

#include <QLoggingCategory>

namespace qpm::test {

namespace {

Q_LOGGING_CATEGORY(lcGuard, "qpm.test.guard")

constexpr char messageNumberVariable[] = "QPM_TEST_SMS_TO";
constexpr char callNumberVariable[] = "QPM_TEST_CALL_TO";
constexpr qsizetype minAllowedNumberLength = 8;

// The phone trims a command like Java's String.trim(): every character up to
// U+0020 goes at both ends.
bool isTrimmed(char16_t c)
{
    return c <= u' ';
}

QString trimmedLikeThePhone(const QString &text)
{
    qsizetype begin = 0;
    qsizetype end = text.size();
    while (begin < end && isTrimmed(text.at(begin).unicode()))
        ++begin;
    while (end > begin && isTrimmed(text.at(end - 1).unicode()))
        --end;
    return text.sliced(begin, end - begin);
}

// "+49 155 0000001" and "01550000001" name the same line. Returns the number
// with a leading 0 or +, or an empty string for anything that is no plain
// number.
QString normalisedNumber(const QString &text)
{
    QString number = text;
    number.remove(u' ');
    if (number.startsWith(QLatin1StringView("+49")))
        number = u'0' + number.sliced(3);
    for (qsizetype i = 0; i < number.size(); ++i) {
        const char16_t c = number.at(i).unicode();
        if (!(c >= u'0' && c <= u'9') && !(i == 0 && c == u'+'))
            return {};
    }
    return number;
}

// Only a full number counts, so that a short or broken value cannot open the
// guard to an exchange or emergency code.
bool isAllowedNumber(const QString &number, const char *variable)
{
    const QString allowed = normalisedNumber(qEnvironmentVariable(variable));
    const bool full = allowed.size() >= minAllowedNumberLength &&
                      (allowed.startsWith(u'0') || allowed.startsWith(u'+'));
    return full && normalisedNumber(number) == allowed;
}

bool isDigits(QStringView text)
{
    if (text.isEmpty())
        return false;
    for (const QChar c : text) {
        if (c.unicode() < u'0' || c.unicode() > u'9')
            return false;
    }
    return true;
}

// `arguments` is what follows "ATD": "<number>;" or "<number>;,<sim>".
bool isAllowedCall(const QString &arguments)
{
    const qsizetype semicolon = arguments.indexOf(u';');
    if (semicolon <= 0)
        return false;
    const QStringView rest = QStringView(arguments).sliced(semicolon + 1);
    if (!rest.isEmpty() && !(rest.startsWith(u',') && isDigits(rest.sliced(1))))
        return false;
    return isAllowedNumber(arguments.first(semicolon), callNumberVariable);
}

// `arguments` is what follows "AT+CMGS=": the destination comes first, before
// the text or the flags. The PDU form, a bare length, has no comma.
bool isAllowedMessage(const QString &arguments)
{
    const qsizetype comma = arguments.indexOf(u',');
    if (comma <= 0)
        return false;
    return isAllowedNumber(arguments.first(comma), messageNumberVariable);
}

std::optional<QString> blockReasonForPut(const QByteArray &frame)
{
    const auto packet = core::protocol::ObexPacket::decode(
        frame, core::protocol::ObexPacket::Layout::Plain);
    if (!packet.hasValue())
        return QStringLiteral("OBEX PUT that cannot be checked");
    if (!packet.value().name)
        return std::nullopt;
    const QString name = packet.value().name->toLower();
    if (name.contains(QLatin1StringView("/msg")) &&
        name != QLatin1StringView("telecom/msg/setflags")) {
        return QStringLiteral("OBEX PUT below /msg schedules a text message");
    }
    return std::nullopt;
}

} // namespace

using core::Error;
using core::ErrorCode;
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
    const QString upper = trimmedLikeThePhone(command).toUpper();
    if (upper.startsWith(QLatin1StringView("ATD"))) {
        if (isAllowedCall(upper.sliced(3)))
            return std::nullopt;
        return QStringLiteral("ATD places a call");
    }
    if (upper.startsWith(QLatin1StringView("ATA")))
        return QStringLiteral("ATA answers a call");
    if (upper.startsWith(QLatin1StringView("ATH")))
        return QStringLiteral("ATH ends a call");
    if (upper.startsWith(QLatin1StringView("AT+CHUP")))
        return QStringLiteral("AT+CHUP ends a call");
    static constexpr QLatin1StringView sendPrefix("AT+CMGS=");
    if (upper.startsWith(sendPrefix) && upper != QLatin1StringView("AT+CMGS=?")) {
        if (isAllowedMessage(upper.sliced(sendPrefix.size())))
            return std::nullopt;
        return QStringLiteral("AT+CMGS sends a text message");
    }
    if (upper.startsWith(QLatin1StringView("AT*NOTIACTION")))
        return QStringLiteral("AT*NOTIACTION can reply to a message or call back");
    return std::nullopt;
}

std::optional<QString> CommandGuard::blockReasonForFrame(const QByteArray &frame)
{
    // The opcode is checked before any trimming: 0x02, a PUT, is one of the
    // characters the phone would trim from a line. A line that merely starts
    // with that byte fails to decode and is refused as well.
    const quint8 code = frame.isEmpty() ? 0 : quint8(frame.at(0));
    if (code == core::protocol::obex::opPut || code == core::protocol::obex::opPutFinal)
        return blockReasonForPut(frame);

    const QList<QByteArray> lines = frame.split('\n');
    for (const QByteArray &line : lines) {
        const QString command = trimmedLikeThePhone(QString::fromUtf8(line));
        if (!command.startsWith(QLatin1StringView("AT"), Qt::CaseInsensitive))
            continue;
        if (std::optional<QString> reason = blockReason(command))
            return reason;
    }
    return std::nullopt;
}

} // namespace qpm::test
