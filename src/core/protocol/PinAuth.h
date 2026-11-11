#pragma once

#include <QString>

#include <optional>

namespace qpm::core::protocol {

/// The PIN state that the phone client reports in a "*PIN:" line.
struct PinStatus
{
    enum class State {
        /// No PIN needed, or the PIN was accepted.
        AccessOk,
        /// A PIN is needed before any other command.
        AccessProtected,
        /// The PIN just sent was wrong.
        WrongPin,
    };

    State state = State::AccessOk;
    /// Attempts left before the client stops; 0 means locked.
    int remainingTries = 0;
};

/**
 * Returns the value for "AT*PIN=": the upper-case hex MD5 of "hash" followed
 * by the PIN in UTF-8. The result is a secret and must not be logged.
 */
[[nodiscard]] QString pinHash(const QString &pin);

/**
 * Parses "*PIN: ACCESS OK,<n>", "*PIN: ACCESS PROTECTED,<n>" or
 * "*PIN: WRONG PIN,<n>". Returns nothing for any other line.
 */
[[nodiscard]] std::optional<PinStatus> parsePinLine(const QString &line);

} // namespace qpm::core::protocol
