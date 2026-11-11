#pragma once

#include "core/Error.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <optional>

namespace qpm::core::protocol {

/// One line of "adb devices -l".
struct AdbDevice
{
    QString serial;
    /// "device" when usable; otherwise for example "unauthorized" or "offline".
    QString state;
    QString model;
    QString product;
};

/**
 * Runs the Android Debug Bridge to reach a phone over USB: lists devices,
 * forwards local TCP ports to the client's ports and starts the client.
 *
 * adb is only ever started from the absolute path given to the constructor,
 * never through PATH, and with program and arguments kept apart. Each call
 * gives up after 15 seconds with ErrorCode::Timeout, and with
 * ErrorCode::LimitExceeded when adb prints more than 1 MiB on its output or
 * on its error output. A path that is not absolute or not executable makes
 * every call fail with ErrorCode::InvalidInput; a serial outside
 * [A-Za-z0-9._:-] as well.
 */
class AdbBridge : public QObject
{
    Q_OBJECT

public:
    explicit AdbBridge(const QString &adbPath, QObject *parent = nullptr);

    /// Runs "adb start-server".
    QFuture<Result<void>> startServer();

    /// Runs "adb devices -l".
    QFuture<Result<QList<AdbDevice>>> devices();

    /**
     * Forwards a free local TCP port to `phonePort` on the device and returns
     * the local port that adb picked.
     */
    QFuture<Result<quint16>> forward(const QString &serial, quint16 phonePort);

    /// Removes the forward of `localPort`.
    QFuture<Result<void>> removeForward(const QString &serial, quint16 localPort);

    /**
     * Starts the phone client in the background. Fails with
     * ErrorCode::NotFound when the client is not installed.
     */
    QFuture<Result<void>> startClient(const QString &serial);

    /// Parses the output of "adb devices -l"; daemon notices are skipped.
    [[nodiscard]] static Result<QList<AdbDevice>> parseDevices(const QByteArray &output);

    /// Parses the port that "adb forward tcp:0 ..." prints.
    [[nodiscard]] static std::optional<quint16> parseForwardPort(const QByteArray &output);

    [[nodiscard]] static bool isValidSerial(const QString &serial);

private:
    QFuture<Result<QByteArray>> run(const QStringList &arguments);

    QString m_adbPath;
};

} // namespace qpm::core::protocol
