#pragma once

#include <QString>

#include <memory>
#include <utility>

namespace qpm::core {

/**
 * Machine-readable reason of a failure. The user interface maps each code to
 * its own text; the message of an Error is technical and English.
 */
enum class ErrorCode {
    None,
    Timeout,
    ConnectionClosed,
    ProtocolError,
    AccessDenied,
    WrongPin,
    PermissionDenied,
    NotSupported,
    NotFound,
    /// The phone refused the request, for example with "+CMS ERROR" or an
    /// OBEX conflict.
    Rejected,
    AlreadyConnected,
    WifiDisabled,
    IoFailure,
    InvalidInput,
    LimitExceeded,
    Cancelled,
};

/**
 * A failure with a code and a message. A default-constructed Error means "no
 * error". The message never contains personal data such as names, numbers,
 * message texts or the PIN.
 */
class [[nodiscard]] Error
{
public:
    Error() = default;
    Error(ErrorCode code, QString message) : m_code(code), m_message(std::move(message)) {}

    [[nodiscard]] ErrorCode code() const { return m_code; }
    [[nodiscard]] QString message() const { return m_message; }
    [[nodiscard]] bool isError() const { return m_code != ErrorCode::None; }

private:
    ErrorCode m_code = ErrorCode::None;
    QString m_message;
};

/**
 * Either a value or an Error. value() may only be called when hasValue() is
 * true. error() is a default Error, meaning "no error", while a value is held.
 */
template <typename T> class [[nodiscard]] Result
{
public:
    // Implicit on purpose, although single-argument constructors are
    // otherwise explicit: a function returning Result<T> can then write both
    // "return value;" and "return Error(...);".
    Result(T value) : m_value(std::make_shared<const T>(std::move(value))) {}
    // Implicit for the same reason as above.
    Result(Error error) : m_error(std::move(error)) {}

    [[nodiscard]] bool hasValue() const { return m_value != nullptr; }
    [[nodiscard]] const T &value() const
    {
        Q_ASSERT(m_value);
        return *m_value;
    }
    [[nodiscard]] const Error &error() const { return m_error; }

private:
    // The value lives behind a pointer that is always initialized. With an
    // optional or a variant in place, GCC 15 at -O3 reports the storage of the
    // absent value as maybe uninitialized once a Result holding an Error is
    // moved into a QFuture. Copies share the value, which nothing can change.
    std::shared_ptr<const T> m_value;
    Error m_error;
};

/**
 * Success without a value, or an Error. A default-constructed Result<void>
 * means success.
 */
template <> class [[nodiscard]] Result<void>
{
public:
    Result() = default;
    // Implicit on purpose so that a function returning Result<void> can write
    // "return Error(...);".
    Result(Error error) : m_error(std::move(error)) {}

    [[nodiscard]] bool hasValue() const { return !m_error.isError(); }
    [[nodiscard]] const Error &error() const { return m_error; }

private:
    Error m_error;
};

} // namespace qpm::core
