#include "core/protocol/AdbBridge.h"

#include "core/Logging.h"
#include "core/codec/Limits.h"

#include <QFileInfo>
#include <QProcess>
#include <QPromise>
#include <QTimer>

#include <chrono>
#include <limits>
#include <memory>

namespace qpm::core::protocol {

namespace {

constexpr std::chrono::seconds processTimeout{15};
constexpr qsizetype maxSerialLength = 128;
constexpr qsizetype maxErrorDetail = 200;

QString clientComponent()
{
    return QStringLiteral("com.fjsoft.myphoneexplorer.client/.MainActivity");
}

Error invalidSerial()
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("Invalid adb serial number"));
}

QString firstLine(const QByteArray &text)
{
    const qsizetype end = text.indexOf('\n');
    return QString::fromUtf8(end < 0 ? text : text.left(end)).trimmed().left(maxErrorDetail);
}

} // namespace

AdbBridge::AdbBridge(const QString &adbPath, QObject *parent) : QObject(parent), m_adbPath(adbPath)
{
}

QFuture<Result<void>> AdbBridge::startServer()
{
    return run({QStringLiteral("start-server")})
        .then(this, [](const Result<QByteArray> &output) -> Result<void> {
            if (!output.hasValue())
                return output.error();
            return {};
        });
}

QFuture<Result<QList<AdbDevice>>> AdbBridge::devices()
{
    return run({QStringLiteral("devices"), QStringLiteral("-l")})
        .then(this, [](const Result<QByteArray> &output) -> Result<QList<AdbDevice>> {
            if (!output.hasValue())
                return output.error();
            return parseDevices(output.value());
        });
}

QFuture<Result<quint16>> AdbBridge::forward(const QString &serial, quint16 phonePort)
{
    if (!isValidSerial(serial))
        return QtFuture::makeReadyValueFuture(Result<quint16>(invalidSerial()));
    return run({QStringLiteral("-s"), serial, QStringLiteral("forward"), QStringLiteral("tcp:0"),
                QStringLiteral("tcp:%1").arg(phonePort)})
        .then(this, [](const Result<QByteArray> &output) -> Result<quint16> {
            if (!output.hasValue())
                return output.error();
            const std::optional<quint16> port = parseForwardPort(output.value());
            if (!port)
                return Error(ErrorCode::ProtocolError,
                             QStringLiteral("adb forward printed no port"));
            return *port;
        });
}

QFuture<Result<void>> AdbBridge::removeForward(const QString &serial, quint16 localPort)
{
    if (!isValidSerial(serial))
        return QtFuture::makeReadyValueFuture(Result<void>(invalidSerial()));
    return run({QStringLiteral("-s"), serial, QStringLiteral("forward"), QStringLiteral("--remove"),
                QStringLiteral("tcp:%1").arg(localPort)})
        .then(this, [](const Result<QByteArray> &output) -> Result<void> {
            if (!output.hasValue())
                return output.error();
            return {};
        });
}

QFuture<Result<void>> AdbBridge::startClient(const QString &serial)
{
    if (!isValidSerial(serial))
        return QtFuture::makeReadyValueFuture(Result<void>(invalidSerial()));
    return run({QStringLiteral("-s"), serial, QStringLiteral("shell"), QStringLiteral("am"),
                QStringLiteral("start"), QStringLiteral("-n"), clientComponent(),
                QStringLiteral("--ez"), QStringLiteral("adb"), QStringLiteral("true")})
        .then(this, [](const Result<QByteArray> &output) -> Result<void> {
            if (!output.hasValue())
                return output.error();
            // "am start" reports a missing activity on its output and still
            // exits with 0.
            if (output.value().contains("Error:"))
                return Error(ErrorCode::NotFound,
                             QStringLiteral("The phone client is not installed"));
            return {};
        });
}

Result<QList<AdbDevice>> AdbBridge::parseDevices(const QByteArray &output)
{
    QList<AdbDevice> devices;
    bool headerSeen = false;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &rawLine : lines) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith('*'))
            continue;
        if (line.startsWith("List of devices attached")) {
            headerSeen = true;
            continue;
        }
        if (!headerSeen)
            continue;
        const QList<QByteArray> fields = line.simplified().split(' ');
        if (fields.size() < 2)
            continue;
        AdbDevice device;
        device.serial = QString::fromUtf8(fields.at(0));
        device.state = QString::fromUtf8(fields.at(1));
        if (!isValidSerial(device.serial))
            continue;
        for (qsizetype i = 2; i < fields.size(); ++i) {
            const QByteArray &field = fields.at(i);
            if (field.startsWith("model:"))
                device.model = QString::fromUtf8(field.mid(6));
            else if (field.startsWith("product:"))
                device.product = QString::fromUtf8(field.mid(8));
        }
        devices.append(device);
    }
    if (!headerSeen)
        return Error(ErrorCode::ProtocolError, QStringLiteral("Unexpected output of adb devices"));
    return devices;
}

std::optional<quint16> AdbBridge::parseForwardPort(const QByteArray &output)
{
    bool ok = false;
    const uint port = output.trimmed().toUInt(&ok);
    if (!ok || port == 0 || port > std::numeric_limits<quint16>::max())
        return std::nullopt;
    return quint16(port);
}

bool AdbBridge::isValidSerial(const QString &serial)
{
    if (serial.isEmpty() || serial.size() > maxSerialLength || serial.startsWith(u'-'))
        return false;
    for (const QChar c : serial) {
        const char16_t code = c.unicode();
        const bool allowed = (code >= u'a' && code <= u'z') || (code >= u'A' && code <= u'Z') ||
                             (code >= u'0' && code <= u'9') || code == u'.' || code == u'_' ||
                             code == u':' || code == u'-';
        if (!allowed)
            return false;
    }
    return true;
}

QFuture<Result<QByteArray>> AdbBridge::run(const QStringList &arguments)
{
    const QFileInfo program(m_adbPath);
    if (!program.isAbsolute() || !program.isFile() || !program.isExecutable()) {
        return QtFuture::makeReadyValueFuture(Result<QByteArray>(
            Error(ErrorCode::InvalidInput,
                  QStringLiteral("adb must be given as an absolute path to an executable"))));
    }

    struct State
    {
        QPromise<Result<QByteArray>> promise;
        QByteArray output;
        QByteArray errors;
        bool done = false;
    };
    auto state = std::make_shared<State>();
    state->promise.start();
    QFuture<Result<QByteArray>> future = state->promise.future();

    auto *process = new QProcess(this);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);

    const auto finish = [state, process](const Result<QByteArray> &result) {
        if (state->done)
            return;
        state->done = true;
        state->promise.addResult(result);
        state->promise.finish();
        process->deleteLater();
    };

    connect(process, &QProcess::readyReadStandardOutput, process, [state, process, finish]() {
        state->output += process->readAllStandardOutput();
        if (state->output.size() > limits::maxProcessOutputBytes) {
            process->kill();
            finish(Error(ErrorCode::LimitExceeded, QStringLiteral("adb printed too much")));
        }
    });
    connect(process, &QProcess::readyReadStandardError, process, [state, process]() {
        state->errors += process->readAllStandardError().left(limits::maxProcessOutputBytes);
    });
    connect(process, &QProcess::errorOccurred, process, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(Error(ErrorCode::IoFailure, QStringLiteral("adb could not be started")));
    });
    connect(process, &QProcess::finished, process,
            [state, process, finish](int exitCode, QProcess::ExitStatus status) {
                state->output += process->readAllStandardOutput();
                if (status != QProcess::NormalExit || exitCode != 0) {
                    finish(Error(ErrorCode::IoFailure, QStringLiteral("adb failed (exit %1): %2")
                                                           .arg(exitCode)
                                                           .arg(firstLine(state->errors))));
                    return;
                }
                finish(state->output);
            });
    connect(timer, &QTimer::timeout, process, [process, finish]() {
        process->kill();
        finish(Error(ErrorCode::Timeout,
                     QStringLiteral("adb did not finish within %1 s").arg(processTimeout.count())));
    });

    qCDebug(lcProtocol) << "adb"
                        << arguments.value(arguments.indexOf(QStringLiteral("-s")) == 0 ? 2 : 0);
    timer->start(processTimeout);
    process->start(m_adbPath, arguments, QIODevice::ReadOnly);
    return future;
}

} // namespace qpm::core::protocol
