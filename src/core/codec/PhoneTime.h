#pragma once

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringView>

#include <optional>

namespace qpm::core::codec {

/**
 * Parses "yy/MM/dd,HH:mm:ss" in the local time of this computer, the form of
 * the call log. Two-digit years are read as 2000 to 2099. Returns nothing for
 * any other form or an impossible date.
 */
[[nodiscard]] std::optional<QDateTime> parseCallLogTime(QStringView text);

/// Formats a time as "yy/MM/dd,HH:mm:ss" in local time.
[[nodiscard]] QString formatCallLogTime(const QDateTime &time);

/**
 * Parses "yyyyMMddTHHmmss" without a zone suffix as local time, the form of
 * messages and notifications. Returns nothing for any other form.
 */
[[nodiscard]] std::optional<QDateTime> parseLocalStamp(QStringView text);

/// Formats a time as "yyyyMMddTHHmmss" in local time.
[[nodiscard]] QString formatLocalStamp(const QDateTime &time);

/**
 * Parses "yyyyMMddTHHmmssZ" as UTC. The trailing "Z" is required. Returns
 * nothing for any other form.
 */
[[nodiscard]] std::optional<QDateTime> parseUtcStamp(QStringView text);

/// Formats a time as "yyyyMMddTHHmmssZ" in UTC.
[[nodiscard]] QString formatUtcStamp(const QDateTime &time);

/// Parses a date "yyyyMMdd", the form of all-day events.
[[nodiscard]] std::optional<QDate> parseDate(QStringView text);

/// Formats a date as "yyyyMMdd".
[[nodiscard]] QString formatDate(QDate date);

} // namespace qpm::core::codec
