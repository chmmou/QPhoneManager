#include "core/codec/CallLogCodec.h"

#include "core/codec/CsvTable.h"
#include "core/codec/PhoneTime.h"

#include <QRegularExpression>

namespace qpm::core::codec {

namespace {

constexpr int firstCallLogYear = 2000;
constexpr int lastCallLogYear = 2099;

QString storageOf(CallDirection direction)
{
    switch (direction) {
    case CallDirection::Incoming:
        return QStringLiteral("RC");
    case CallDirection::Missed:
        return QStringLiteral("MC");
    case CallDirection::Outgoing:
        break;
    }
    return QStringLiteral("DC");
}

bool hasControlCharacter(QStringView text)
{
    for (const QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F)
            return true;
    }
    return false;
}

// Number and name go between quotes, and the phone splits the command at
// commas outside quotes; a quote inside would end the field early.
bool fitsBetweenQuotes(QStringView text, qsizetype maxLength)
{
    return text.size() <= maxLength && !text.contains(u'"') && !hasControlCharacter(text);
}

Error malformed(const QString &what)
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("Call log line: %1").arg(what));
}

const QRegularExpression &linePattern()
{
    // Number and name are not escaped by the phone. The number ends at the
    // first quote that is followed by the address type, the name at the last
    // quote that is followed by the time.
    static const QRegularExpression pattern(
        QStringLiteral("^(\\d{1,10}),\"(.*?)\",(128|SIM\\d{1,2}|\\d{1,5}),\"(.*)\","
                       "\"(\\d\\d/\\d\\d/\\d\\d,\\d\\d:\\d\\d:\\d\\d)\",(\\d{1,12})$"));
    return pattern;
}

QString directionText(CallDirection direction, CsvLanguage language)
{
    const bool german = language == CsvLanguage::German;
    switch (direction) {
    case CallDirection::Incoming:
        return german ? QStringLiteral("Eingehend") : QStringLiteral("Incoming");
    case CallDirection::Missed:
        return german ? QStringLiteral("Verpasst") : QStringLiteral("Missed");
    case CallDirection::Outgoing:
        break;
    }
    return german ? QStringLiteral("Ausgehend") : QStringLiteral("Outgoing");
}

} // namespace

QString selectCallListCommand(CallDirection direction)
{
    return QStringLiteral("AT+CPBS=\"") + storageOf(direction) + u'"';
}

QString readCallListCommand()
{
    return QStringLiteral("AT+CPBR=,");
}

Result<CallEntry> parseCallLine(const QString &line, CallDirection direction)
{
    static const QString prefix = QStringLiteral("+CPBR:");
    if (!line.startsWith(prefix))
        return malformed(QStringLiteral("no +CPBR prefix"));
    if (hasControlCharacter(line))
        return malformed(QStringLiteral("control character"));
    const QString rest = line.mid(prefix.size()).trimmed();
    const QRegularExpressionMatch match = linePattern().match(rest);
    if (!match.hasMatch())
        return malformed(QStringLiteral("unknown form"));

    CallEntry entry;
    entry.direction = direction;
    bool ok = false;
    const qulonglong id = match.capturedView(1).toULongLong(&ok);
    if (!ok || id == 0 || id > maxCallId)
        return malformed(QStringLiteral("id out of range"));
    entry.id = quint32(id);

    entry.number = match.captured(2);
    entry.name = match.captured(4);
    if (entry.number.size() > maxCallNumberLength || entry.name.size() > maxCallNameLength)
        return malformed(QStringLiteral("number or name too long"));

    const QStringView addressType = match.capturedView(3);
    if (addressType.startsWith(u"SIM"))
        entry.simSlot = addressType.mid(3).toInt();

    const std::optional<QDateTime> start = parseCallLogTime(match.capturedView(5));
    if (!start)
        return malformed(QStringLiteral("impossible time"));
    entry.start = *start;

    const qlonglong duration = match.capturedView(6).toLongLong(&ok);
    if (!ok || duration > maxCallDurationSeconds)
        return malformed(QStringLiteral("duration out of range"));
    entry.durationSeconds = duration;
    return entry;
}

Result<CallList> parseCallList(const QStringList &lines, CallDirection direction)
{
    if (lines.size() > maxCallEntries) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("The call list has %1 lines, at most %2 are read")
                         .arg(lines.size())
                         .arg(maxCallEntries));
    }
    CallList list;
    list.entries.reserve(lines.size());
    for (const QString &line : lines) {
        const Result<CallEntry> entry = parseCallLine(line, direction);
        if (entry.hasValue())
            list.entries.append(entry.value());
        else
            ++list.malformed;
    }
    return list;
}

Result<QString> encodeCallWrite(const CallEntry &entry)
{
    if (entry.id > maxCallId)
        return Error(ErrorCode::InvalidInput, QStringLiteral("Call id out of range"));
    if (!fitsBetweenQuotes(entry.number, maxCallNumberLength) ||
        !fitsBetweenQuotes(entry.name, maxCallNameLength)) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("Number or name holds a quote, a control character or is "
                                    "too long"));
    }
    const QDateTime local = entry.start.toLocalTime();
    if (!local.isValid() || local.date().year() < firstCallLogYear ||
        local.date().year() > lastCallLogYear) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("The phone takes call times from %1 to %2 only")
                         .arg(firstCallLogYear)
                         .arg(lastCallLogYear));
    }
    if (entry.durationSeconds < 0 || entry.durationSeconds > maxCallDurationSeconds)
        return Error(ErrorCode::InvalidInput, QStringLiteral("Call duration out of range"));

    return QStringLiteral("AT+CPBW=") + QString::number(entry.id) + QStringLiteral(",\"") +
           entry.number + QStringLiteral("\",128,\"") + entry.name + QStringLiteral("\",\"") +
           formatCallLogTime(local) + QStringLiteral("\",") +
           QString::number(entry.durationSeconds) + QStringLiteral(",\"") +
           storageOf(entry.direction) + u'"';
}

Result<QString> encodeCallDelete(quint32 id)
{
    if (id == 0 || id > maxCallId)
        return Error(ErrorCode::InvalidInput, QStringLiteral("Call id out of range"));
    return QStringLiteral("AT+CPBD=") + QString::number(id);
}

QByteArray writeCallsCsv(const QList<CallEntry> &entries, CsvLanguage language)
{
    const bool german = language == CsvLanguage::German;
    CsvRows rows;
    rows.reserve(entries.size() + 1);
    rows.append(german ? QStringList{QStringLiteral("Art"), QStringLiteral("Name"),
                                     QStringLiteral("Nummer"), QStringLiteral("Zeit"),
                                     QStringLiteral("Dauer (Sekunden)"), QStringLiteral("SIM")}
                       : QStringList{QStringLiteral("Type"), QStringLiteral("Name"),
                                     QStringLiteral("Number"), QStringLiteral("Time"),
                                     QStringLiteral("Duration (seconds)"), QStringLiteral("SIM")});
    for (const CallEntry &entry : entries) {
        rows.append({directionText(entry.direction, language), entry.name, entry.number,
                     entry.start.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                     QString::number(entry.durationSeconds),
                     entry.simSlot >= 0 ? QString::number(entry.simSlot + 1) : QString()});
    }
    return writeCsv(rows);
}

} // namespace qpm::core::codec
