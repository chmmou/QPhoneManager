#pragma once

#include "core/Error.h"
#include "core/codec/VCardInterchange.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace qpm::core::codec {

/// Which of the three call lists of the phone an entry belongs to.
enum class CallDirection { Outgoing, Incoming, Missed };

/// One entry of the call log of the phone.
struct CallEntry
{
    /// The row of the phone's call log; 1 to maxCallId, 0 for an entry not written yet.
    quint32 id = 0;
    /// As the phone stores it; empty for a hidden or unknown number.
    QString number;
    /// The name the phone keeps with the entry; often empty.
    QString name;
    CallDirection direction = CallDirection::Outgoing;
    /// Local time of this computer, to the second.
    QDateTime start;
    qint64 durationSeconds = 0;
    /// Index of the SIM card; -1 when the phone does not say.
    int simSlot = -1;

    bool operator==(const CallEntry &other) const = default;
};

/// Largest id taken from the phone, so that it fits into an int on the way to QML.
inline constexpr quint32 maxCallId = 2147483647;
/// Longest number and name taken from or sent to the phone, in characters.
inline constexpr qsizetype maxCallNumberLength = 64;
inline constexpr qsizetype maxCallNameLength = 512;
/// Longest duration taken from or sent to the phone: 100 days.
inline constexpr qint64 maxCallDurationSeconds = 100LL * 24 * 60 * 60;
/// Most entries read from one call list.
inline constexpr qsizetype maxCallEntries = 100000;

/// The entries of one list and the number of reply lines that could not be read.
struct CallList
{
    QList<CallEntry> entries;
    int malformed = 0;
};

/// "AT+CPBS=\"DC\"", "AT+CPBS=\"RC\"" or "AT+CPBS=\"MC\"": selects the list of `direction`.
[[nodiscard]] QString selectCallListCommand(CallDirection direction);

/// "AT+CPBR=,": reads every entry of the selected list, newest first.
[[nodiscard]] QString readCallListCommand();

/**
 * Reads one reply line of AT+CPBR:
 * +CPBR: <id>,"<number>",<128 or SIM<n>>,"<name>","<yy/MM/dd,HH:mm:ss>",<seconds>
 *
 * The phone does not escape quotes in number and name, so the line is taken
 * apart from both ends. Fails with ErrorCode::InvalidInput for any other
 * form, a control character, an id outside 1 to maxCallId, an impossible
 * time, and a number, name or duration above the limits of this file.
 */
[[nodiscard]] Result<CallEntry> parseCallLine(const QString &line, CallDirection direction);

/**
 * Reads the reply lines of AT+CPBR for the list of `direction`. Lines that
 * cannot be read are left out and counted. Fails with ErrorCode::LimitExceeded
 * when there are more than maxCallEntries lines.
 */
[[nodiscard]] Result<CallList> parseCallList(const QStringList &lines, CallDirection direction);

/**
 * The AT+CPBW command that writes `entry` into the phone's call log: a new
 * row for id 0, otherwise the row with that id. Fails with
 * ErrorCode::InvalidInput for a number or name with a quote or a control
 * character, a time outside the years 2000 to 2099, a negative duration, and
 * values above the limits of this file.
 */
[[nodiscard]] Result<QString> encodeCallWrite(const CallEntry &entry);

/**
 * The AT+CPBD command that deletes the row `id`. Fails with
 * ErrorCode::InvalidInput for an id outside 1 to maxCallId.
 */
[[nodiscard]] Result<QString> encodeCallDelete(quint32 id);

/**
 * Writes the entries as a table: type, name, number, local date and time,
 * duration in seconds and the SIM card counted from 1, empty when unknown.
 * Column titles and type names are in `language`. See writeCsv() for the form.
 */
[[nodiscard]] QByteArray writeCallsCsv(const QList<CallEntry> &entries, CsvLanguage language);

} // namespace qpm::core::codec
