#include "CommandGuard.h"

#include "core/codec/AppListCodec.h"
#include "core/protocol/ObexPacket.h"

#include <QLoggingCategory>

#include <algorithm>
#include <array>

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

// `command` is trimmed like the phone does. The package name keeps its case.
bool isAllowedAppCommand(const QString &command)
{
    static constexpr QLatin1StringView list("AT*EJAVA=1");
    static constexpr QLatin1StringView start("AT*EJAVA=4,");
    static constexpr QLatin1StringView remove("AT*EJAVA=2,");
    if (command.compare(list, Qt::CaseInsensitive) == 0)
        return true;
    for (const QLatin1StringView prefix : {start, remove}) {
        if (command.startsWith(prefix, Qt::CaseInsensitive))
            return core::codec::isValidPackageName(QStringView(command).sliced(prefix.size()));
    }
    return false;
}

// `command` is trimmed and in upper case. The phone resolves the name of a
// command as the text before "=" and "?", and compares it as a whole.
bool isScreenCommand(const QString &command)
{
    static constexpr std::array names = {
        QLatin1StringView("AT*STARTCAPTURESERVER"),
        QLatin1StringView("AT*C"),
        QLatin1StringView("AT*K"),
        QLatin1StringView("AT*T"),
        QLatin1StringView("AT*SELTEXT"),
        QLatin1StringView("AT*KBRD"),
        QLatin1StringView("AT*SHOWACCSETTINGS"),
    };
    const QString name = command.section(u'=', 0, 0).section(u'?', 0, 0);
    return std::find(names.cbegin(), names.cend(), name) != names.cend();
}

// `command` is trimmed and in upper case. Dismissing a notification acts on
// what the owner of a phone has not read yet, and a clip written to a phone
// replaces whatever its owner copied there.
std::optional<QString> emulatorOnlyReason(const QString &command)
{
    const QString name = command.section(u'=', 0, 0).section(u'?', 0, 0);
    if (name == QLatin1StringView("AT*NOTIC"))
        return QStringLiteral("AT*NOTIC dismisses notifications only on an emulator");
    if (command.startsWith(QLatin1StringView("AT*CLIPBOARD=")))
        return QStringLiteral("AT*CLIPBOARD= writes the clipboard only on an emulator");
    return std::nullopt;
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

CommandGuard::CommandGuard(std::unique_ptr<Transport> inner, Target target, QObject *parent)
    : Transport(parent), m_inner(std::move(inner)), m_target(target)
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
    const std::optional<QString> reason = blockReasonForFrame(frame, m_target);
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

std::optional<QString> CommandGuard::blockReason(const QString &command, Target target)
{
    const QString upper = trimmedLikeThePhone(command).toUpper();
    if (target != Target::Emulator && isScreenCommand(upper))
        return QStringLiteral("screen capture and input only on an emulator");
    if (target != Target::Emulator) {
        if (std::optional<QString> reason = emulatorOnlyReason(upper))
            return reason;
    }
    if (upper.startsWith(QLatin1StringView("ATD"))) {
        if (isAllowedCall(upper.sliced(3)))
            return std::nullopt;
        return QStringLiteral("ATD places a call");
    }
    // Hanging up with ATH or AT+CHUP stays allowed, so that a call test can
    // end the call it placed.
    if (upper.startsWith(QLatin1StringView("ATA")))
        return QStringLiteral("ATA answers a call");
    static constexpr QLatin1StringView sendPrefix("AT+CMGS=");
    if (upper.startsWith(sendPrefix) && upper != QLatin1StringView("AT+CMGS=?")) {
        if (isAllowedMessage(upper.sliced(sendPrefix.size())))
            return std::nullopt;
        return QStringLiteral("AT+CMGS sends a text message");
    }
    if (upper.startsWith(QLatin1StringView("AT*NOTIACTION")))
        return QStringLiteral("AT*NOTIACTION can reply to a message or call back");
    if (upper.startsWith(QLatin1StringView("AT*EJAVA")) &&
        !isAllowedAppCommand(trimmedLikeThePhone(command))) {
        return QStringLiteral("AT*EJAVA in a form other than the list, a start or a removal");
    }
    return std::nullopt;
}

std::optional<QString> CommandGuard::blockReasonForFrame(const QByteArray &frame, Target target)
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
        if (std::optional<QString> reason = blockReason(command, target))
            return reason;
    }
    return std::nullopt;
}

} // namespace qpm::test
