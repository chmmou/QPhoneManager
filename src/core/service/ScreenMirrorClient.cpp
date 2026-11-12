#include "core/service/ScreenMirrorClient.h"

#include "core/Logging.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/DeviceStatusService.h"

#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <utility>

namespace qpm::core::service {

namespace {

using protocol::AtReply;
using protocol::PhoneConnection;

constexpr QByteArrayView fileBegin("FileBegin;");
constexpr QByteArrayView fileEnd("\r\nFileEnd");
constexpr QByteArrayView jpegEnd("\xFF\xD9", 2);
constexpr QByteArrayView pngEnd("IEND\xAE\x42\x60\x82", 8);
constexpr int maxRotation = 3;
constexpr int maxQuality = 100;
constexpr qsizetype headerFields = 5;
constexpr std::chrono::seconds firstFrameTimeout{10};
constexpr std::chrono::seconds inputTimeout{15};

Error malformed(const QString &what)
{
    return Error(ErrorCode::ProtocolError, QStringLiteral("Screen stream: %1").arg(what));
}

Error notConnected()
{
    return Error(ErrorCode::ConnectionClosed, QStringLiteral("The phone is not connected"));
}

template <typename T> QFuture<Result<T>> failed(const Error &error)
{
    return QtFuture::makeReadyValueFuture(Result<T>(error));
}

std::optional<int> boundedInt(QByteArrayView text, int minimum, int maximum)
{
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok || value < minimum || value > maximum)
        return std::nullopt;
    return value;
}

std::optional<QSize> sizeField(QByteArrayView text)
{
    const qsizetype cross = text.indexOf('x');
    if (cross < 0)
        return std::nullopt;
    const std::optional<int> width = boundedInt(text.first(cross), 1, limits::maxImageWidth);
    const std::optional<int> height = boundedInt(text.sliced(cross + 1), 1, limits::maxImageHeight);
    if (!width || !height)
        return std::nullopt;
    return QSize(*width, *height);
}

// Errors for commands whose reply carries no more than OK or a refusal.
// `what` names the request without its arguments.
Error replyError(const AtReply &reply, const QString &what)
{
    switch (reply.status) {
    case AtReply::Status::Ok:
        return {};
    case AtReply::Status::Timeout:
        return Error(ErrorCode::Timeout, QStringLiteral("The phone did not answer %1").arg(what));
    case AtReply::Status::Closed:
    case AtReply::Status::AccessDenied:
        return notConnected();
    case AtReply::Status::PermissionDenied:
        return Error(ErrorCode::PermissionDenied,
                     QStringLiteral("The phone takes no input for %1 while its accessibility "
                                    "service is off")
                         .arg(what));
    case AtReply::Status::NotSupported:
        return Error(ErrorCode::NotSupported,
                     QStringLiteral("The phone does not support %1").arg(what));
    case AtReply::Status::Dump:
    case AtReply::Status::CmsError:
    case AtReply::Status::Error:
        break;
    }
    return Error(ErrorCode::Rejected, QStringLiteral("The phone refused %1").arg(what));
}

// Sends `command` and hands its reply to `done`; a session that is gone or
// ends first gives a reply with status Closed.
void sendCommand(PhoneConnection *connection, QObject *context, const QString &command,
                 std::chrono::milliseconds timeout, std::function<void(const AtReply &)> done)
{
    const auto closed = [command]() {
        AtReply reply;
        reply.status = AtReply::Status::Closed;
        reply.command = command;
        return reply;
    };
    protocol::AtSession *session = connection->session();
    if (!session) {
        done(closed());
        return;
    }
    session->send(command, timeout)
        .then(context, [done](const AtReply &reply) { done(reply); })
        .onCanceled(context, [done, closed]() { done(closed()); });
}

} // namespace

Result<void> ScreenFrameReader::feed(const QByteArray &bytes)
{
    if (m_error.isError())
        return m_error;
    m_buffer += bytes;
    bool progressed = true;
    while (progressed) {
        const Result<void> stepped = step(progressed);
        if (!stepped.hasValue()) {
            m_error = stepped.error();
            m_buffer.clear();
            m_frames.clear();
            return m_error;
        }
    }
    return {};
}

QList<ScreenFrame> ScreenFrameReader::takeFrames()
{
    return std::exchange(m_frames, {});
}

Result<ScreenFrame> ScreenFrameReader::parseHeader(const QByteArray &line)
{
    const QList<QByteArray> fields = line.split(';');
    if (fields.size() != headerFields)
        return malformed(QStringLiteral("header with %1 fields").arg(fields.size()));
    ScreenFrame frame;
    const std::optional<QSize> screen = sizeField(fields.at(0));
    const std::optional<int> rotation = boundedInt(fields.at(1), 0, maxRotation);
    const std::optional<QSize> image = sizeField(fields.at(2));
    const std::optional<int> quality = boundedInt(fields.at(4), 0, maxQuality);
    if (!screen || !image)
        return Error(ErrorCode::LimitExceeded, QStringLiteral("Screen size out of bounds"));
    if (!rotation || !quality)
        return malformed(QStringLiteral("rotation or quality out of range"));
    // The phone pads the rows of its picture but never cuts the screen.
    if (image->width() < screen->width() || image->height() < screen->height())
        return malformed(QStringLiteral("picture smaller than the screen"));
    if (fields.at(3) == "JPG")
        frame.format = ScreenFrame::Format::Jpeg;
    else if (fields.at(3) == "PNG")
        frame.format = ScreenFrame::Format::Png;
    else
        return malformed(QStringLiteral("unknown picture format"));
    frame.screenSize = *screen;
    frame.rotation = *rotation;
    frame.imageSize = *image;
    return frame;
}

Result<void> ScreenFrameReader::step(bool &progressed)
{
    progressed = false;
    if (!m_inImage) {
        if (m_buffer.size() < fileBegin.size()) {
            if (!fileBegin.startsWith(m_buffer))
                return malformed(QStringLiteral("no frame header"));
            return {};
        }
        if (!m_buffer.startsWith(fileBegin))
            return malformed(QStringLiteral("no frame header"));
        const qsizetype lineEnd = m_buffer.indexOf("\r\n");
        if (lineEnd > maxHeaderBytes || (lineEnd < 0 && m_buffer.size() > maxHeaderBytes))
            return Error(ErrorCode::LimitExceeded, QStringLiteral("Screen frame header too long"));
        if (lineEnd < 0)
            return {};
        const Result<ScreenFrame> header =
            parseHeader(m_buffer.sliced(fileBegin.size(), lineEnd - fileBegin.size()));
        if (!header.hasValue())
            return header.error();
        m_buffer.remove(0, lineEnd + 2);
        m_current = header.value();
        m_inImage = true;
        m_searchFrom = 0;
        progressed = true;
        return {};
    }
    // The picture carries no length. Its end text can occur inside the
    // picture, so it counts only behind the end marker of the format.
    const QByteArrayView marker = m_current.format == ScreenFrame::Format::Jpeg ? jpegEnd : pngEnd;
    for (;;) {
        const qsizetype end = m_buffer.indexOf(fileEnd, m_searchFrom);
        if (end < 0) {
            if (m_buffer.size() > maxImageBytes + fileEnd.size())
                return Error(ErrorCode::LimitExceeded, QStringLiteral("Screen frame too large"));
            m_searchFrom = std::max<qsizetype>(0, m_buffer.size() - fileEnd.size() + 1);
            return {};
        }
        if (end > maxImageBytes)
            return Error(ErrorCode::LimitExceeded, QStringLiteral("Screen frame too large"));
        if (!QByteArrayView(m_buffer).first(end).endsWith(marker)) {
            m_searchFrom = end + 1;
            continue;
        }
        m_current.image = m_buffer.first(end);
        m_buffer.remove(0, end + fileEnd.size());
        m_frames.append(std::exchange(m_current, ScreenFrame()));
        m_inImage = false;
        progressed = true;
        return {};
    }
}

ScreenMirrorClient::ScreenMirrorClient(protocol::PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection), m_frameTimer(new QTimer(this))
{
    Q_ASSERT(connection);
    m_frameTimer->setSingleShot(true);
    connect(m_frameTimer, &QTimer::timeout, this, &ScreenMirrorClient::deliverPendingFrame);
    connect(connection, &PhoneConnection::stateChanged, this, [this](PhoneConnection::State state) {
        if (state == PhoneConnection::State::Ready || state == PhoneConnection::State::Busy)
            return;
        setControl(Control::Unknown);
        fail(Error(ErrorCode::ConnectionClosed,
                   QStringLiteral("The connection to the phone ended")));
    });
}

void ScreenMirrorClient::setEndpointResolver(FileService::EndpointResolver resolver)
{
    m_resolver = std::move(resolver);
}

std::optional<protocol::Endpoint> ScreenMirrorClient::endpoint(quint16 port) const
{
    return m_resolver ? m_resolver(port) : m_connection->sideChannelEndpoint(port);
}

QFuture<Result<void>> ScreenMirrorClient::start()
{
    if (m_state != State::Idle) {
        return failed<void>(
            Error(ErrorCode::InvalidInput, QStringLiteral("The screen is mirrored already")));
    }
    if (!m_connection->session())
        return failed<void>(notConnected());
    if (!endpoint(screenPort)) {
        return failed<void>(Error(ErrorCode::NotSupported,
                                  QStringLiteral("The screen needs a connection over the network "
                                                 "or USB")));
    }
    const quint64 generation = ++m_generation;
    m_startPromise = std::make_shared<QPromise<Result<void>>>();
    m_startPromise->start();
    QFuture<Result<void>> future = m_startPromise->future();
    setState(State::Starting);

    const auto stale = [this, generation]() { return generation != m_generation; };
    const auto consent = [this, stale](const AtReply &reply) {
        if (stale())
            return;
        if (reply.status == AtReply::Status::Ok) {
            const std::optional<protocol::Endpoint> side = endpoint(screenPort);
            if (!side) {
                fail(notConnected());
                return;
            }
            setState(State::Connecting);
            openChannel(*side, m_generation);
            return;
        }
        if (reply.status == AtReply::Status::Error &&
            reply.detail == QLatin1StringView("CANCELED")) {
            fail(Error(ErrorCode::Rejected,
                       QStringLiteral("The capture was declined on the phone")));
        } else if (reply.status == AtReply::Status::Timeout ||
                   (reply.status == AtReply::Status::Error &&
                    reply.detail == QLatin1StringView("TIMEOUT"))) {
            fail(Error(ErrorCode::Timeout,
                       QStringLiteral("Nobody allowed the capture on the phone in time")));
        } else {
            fail(replyError(reply, QStringLiteral("the screen capture")));
        }
    };
    const auto askUser = [this, stale, consent]() {
        if (stale())
            return;
        setState(State::AwaitingConsent);
        sendCommand(m_connection, this, QStringLiteral("AT*STARTCAPTURESERVER"), consentTimeout,
                    consent);
    };
    const auto display = [this, stale, askUser](const AtReply &reply) {
        if (stale())
            return;
        if (const std::optional<DisplayInfo> info = DeviceStatusService::parseDisplay(reply.lines))
            setScreenSize(QSize(info->width, info->height));
        // Control is asked for along the way; mirroring works without it.
        checkControl().then(this, [askUser](const Result<Control> &) { askUser(); });
    };
    sendCommand(m_connection, this, QStringLiteral("AT*STARTCAPTURESERVER=?"), inputTimeout,
                [this, stale, display](const AtReply &reply) {
                    if (stale())
                        return;
                    if (reply.status != AtReply::Status::Ok) {
                        const Error error = replyError(reply, QStringLiteral("a screen capture"));
                        fail(error.code() == ErrorCode::Rejected
                                 ? Error(ErrorCode::NotSupported,
                                         QStringLiteral("The phone cannot capture its screen"))
                                 : error);
                        return;
                    }
                    sendCommand(m_connection, this, QStringLiteral("AT*DISPLAY"), inputTimeout,
                                display);
                });
    return future;
}

void ScreenMirrorClient::stop()
{
    ++m_generation;
    closeChannel();
    settleStart(Error(ErrorCode::Cancelled, QStringLiteral("The mirroring was stopped")));
    setState(State::Idle);
}

QFuture<Result<ScreenMirrorClient::Control>> ScreenMirrorClient::checkControl()
{
    if (!m_connection->session())
        return failed<Control>(notConnected());
    auto promise = std::make_shared<QPromise<Result<Control>>>();
    promise->start();
    sendCommand(m_connection, this, QStringLiteral("AT*K=?"), inputTimeout,
                [this, promise](const AtReply &reply) {
                    Result<Control> result = Control::Available;
                    if (reply.status == AtReply::Status::PermissionDenied) {
                        result = Control::ServiceOff;
                    } else if (reply.status == AtReply::Status::NotSupported) {
                        result = Control::NotSupported;
                    } else if (reply.status != AtReply::Status::Ok) {
                        result = replyError(reply, QStringLiteral("the input check"));
                    }
                    if (result.hasValue())
                        setControl(result.value());
                    promise->addResult(result);
                    promise->finish();
                });
    return promise->future();
}

QFuture<Result<void>> ScreenMirrorClient::showAccessibilitySettings()
{
    return sendInput(QStringLiteral("AT*SHOWACCSETTINGS"),
                     QStringLiteral("to show the accessibility settings"));
}

QFuture<Result<void>> ScreenMirrorClient::pointer(Pointer action, const QPoint &position)
{
    if (const Error error = checkPosition(position); error.isError())
        return failed<void>(error);
    // Mode 0 leaves out the pointer the phone could draw, which needs another permission.
    const int state = action == Pointer::Up ? 0 : 1;
    return sendInput(
        QStringLiteral("AT*C=0,%1,%2,%3").arg(position.x()).arg(position.y()).arg(state),
        QStringLiteral("a touch"));
}

QFuture<Result<void>> ScreenMirrorClient::longPress(const QPoint &position)
{
    if (const Error error = checkPosition(position); error.isError())
        return failed<void>(error);
    return sendInput(QStringLiteral("AT*C=0,%1,%2,2").arg(position.x()).arg(position.y()),
                     QStringLiteral("a long press"));
}

QFuture<Result<void>> ScreenMirrorClient::scroll(const QPoint &position, Scroll direction)
{
    if (const Error error = checkPosition(position); error.isError())
        return failed<void>(error);
    // 3 moves the finger down the screen, which brings up what lies above.
    const int state = direction == Scroll::Up ? 3 : 4;
    return sendInput(
        QStringLiteral("AT*C=0,%1,%2,%3").arg(position.x()).arg(position.y()).arg(state),
        QStringLiteral("scrolling"));
}

QFuture<Result<void>> ScreenMirrorClient::pressKey(Key key)
{
    return sendInput(keyCommand(key), QStringLiteral("a key"));
}

QFuture<Result<void>> ScreenMirrorClient::typeText(const QString &text)
{
    if (text.isEmpty() || text.size() > maxTextLength)
        return failed<void>(Error(ErrorCode::InvalidInput, QStringLiteral("Unusable text length")));
    for (const QChar c : text) {
        if (c.unicode() < u' ' || c.unicode() == 0x7F)
            return failed<void>(
                Error(ErrorCode::InvalidInput, QStringLiteral("Text with a control character")));
    }
    // The phone drops one trailing comma, and needs one behind long text. A
    // comma of its own also keeps a space at the end of the text.
    return sendInput(QStringLiteral("AT*T=") + text + u',', QStringLiteral("text input"));
}

QString ScreenMirrorClient::keyCommand(Key key)
{
    switch (key) {
    case Key::Back:
        return QStringLiteral("AT*K=back");
    case Key::Home:
        return QStringLiteral("AT*K=home");
    case Key::Recents:
        return QStringLiteral("AT*K=recents");
    case Key::Left:
        return QStringLiteral("AT*K=left");
    case Key::Right:
        return QStringLiteral("AT*K=right");
    case Key::Up:
        return QStringLiteral("AT*K=up");
    case Key::Down:
        return QStringLiteral("AT*K=down");
    case Key::Enter:
        return QStringLiteral("AT*K=enter");
    case Key::Backspace:
        break;
    }
    return QStringLiteral("AT*K=del");
}

QFuture<Result<void>> ScreenMirrorClient::sendInput(const QString &command, const QString &what)
{
    if (!m_connection->session())
        return failed<void>(notConnected());
    // A key or text the phone takes proves the accessibility service; a touch
    // does not, since the phone takes it also when it drops it.
    const bool proves = command.startsWith(QLatin1StringView("AT*K=")) ||
                        command.startsWith(QLatin1StringView("AT*T="));
    auto promise = std::make_shared<QPromise<Result<void>>>();
    promise->start();
    sendCommand(m_connection, this, command, inputTimeout,
                [this, promise, what, proves](const AtReply &reply) {
                    const Error error = replyError(reply, what);
                    if (error.code() == ErrorCode::PermissionDenied)
                        setControl(Control::ServiceOff);
                    else if (error.code() == ErrorCode::NotSupported)
                        setControl(Control::NotSupported);
                    else if (!error.isError() && proves)
                        setControl(Control::Available);
                    promise->addResult(Result<void>(error));
                    promise->finish();
                });
    return promise->future();
}

Error ScreenMirrorClient::checkPosition(const QPoint &position) const
{
    const QSize bounds = m_screenSize.isEmpty()
                             ? QSize(limits::maxImageWidth, limits::maxImageHeight)
                             : m_screenSize;
    if (position.x() < 0 || position.y() < 0 || position.x() >= bounds.width() ||
        position.y() >= bounds.height()) {
        return Error(ErrorCode::InvalidInput, QStringLiteral("Position outside the screen"));
    }
    return {};
}

void ScreenMirrorClient::openChannel(const protocol::Endpoint &target, quint64 generation)
{
    closeChannel();
    // Plain TCP, although network connections are meant to use TLS: the phone
    // app offers this channel without it, and the program has to work with that app.
    m_socket = new QTcpSocket(this);
    QTcpSocket *socket = m_socket;
    auto *firstFrame = new QTimer(socket);
    firstFrame->setSingleShot(true);
    connect(firstFrame, &QTimer::timeout, this, [this, generation]() {
        if (generation == m_generation && !m_received)
            fail(Error(ErrorCode::Timeout,
                       QStringLiteral("The phone sent no picture of its screen")));
    });
    connect(socket, &QTcpSocket::connected, socket, [socket, firstFrame]() {
        // Asks for a picture at once; the phone otherwise waits for a change.
        socket->write(QByteArray("0:JPG:") + QByteArray::number(pictureQuality));
        firstFrame->start(firstFrameTimeout);
    });
    connect(socket, &QTcpSocket::readyRead, this, &ScreenMirrorClient::onSocketData);
    // A channel that closes before its first byte did not reach the phone's
    // listener yet, as over adb forwarding; the phone keeps listening.
    auto triesLeft = std::make_shared<int>(FileService::sideChannelConnectTries);
    const auto missed = [this, socket, target, triesLeft, generation]() {
        if (generation != m_generation || socket != m_socket)
            return;
        if (m_received || --*triesLeft <= 0) {
            fail(Error(ErrorCode::ConnectionClosed,
                       m_received ? QStringLiteral("The screen stream broke off")
                                  : QStringLiteral("The screen channel cannot be reached")));
            return;
        }
        QTimer::singleShot(FileService::sideChannelRetryPause, socket,
                           [socket, target]() { socket->connectToHost(target.host, target.port); });
    };
    connect(socket, &QTcpSocket::disconnected, this, missed);
    connect(socket, &QTcpSocket::errorOccurred, this, [socket, missed]() {
        // A close by the phone also ends in disconnected().
        if (socket->error() != QAbstractSocket::RemoteHostClosedError &&
            socket->state() != QAbstractSocket::ConnectedState) {
            missed();
        }
    });
    socket->connectToHost(target.host, target.port);
}

void ScreenMirrorClient::onSocketData()
{
    if (!m_socket)
        return;
    const QByteArray data = m_socket->readAll();
    if (!m_received && !data.isEmpty()) {
        m_received = true;
        setState(State::Running);
        settleStart({});
    }
    const Result<void> fed = m_reader.feed(data);
    if (!fed.hasValue()) {
        qCWarning(lcProtocol) << "Screen stream refused:" << fed.error().message();
        fail(fed.error());
        return;
    }
    onFrames(m_reader.takeFrames());
}

void ScreenMirrorClient::onFrames(QList<ScreenFrame> frames)
{
    if (frames.isEmpty())
        return;
    ScreenFrame newest = std::move(frames.last());
    setScreenSize(newest.screenSize);
    if (m_frameTimer->isActive()) {
        m_pendingFrame = std::move(newest);
        return;
    }
    m_frameTimer->start(minFrameInterval);
    Q_EMIT frameReceived(newest);
}

void ScreenMirrorClient::deliverPendingFrame()
{
    if (!m_pendingFrame)
        return;
    const ScreenFrame frame = std::move(*m_pendingFrame);
    m_pendingFrame.reset();
    m_frameTimer->start(minFrameInterval);
    Q_EMIT frameReceived(frame);
}

void ScreenMirrorClient::closeChannel()
{
    if (m_socket) {
        QTcpSocket *socket = std::exchange(m_socket, nullptr);
        socket->disconnect(this);
        // Closing the channel is what makes the phone stop capturing.
        socket->abort();
        socket->deleteLater();
    }
    m_reader = ScreenFrameReader();
    m_pendingFrame.reset();
    m_frameTimer->stop();
    m_received = false;
}

void ScreenMirrorClient::fail(const Error &error)
{
    if (m_state == State::Idle)
        return;
    ++m_generation;
    closeChannel();
    const bool starting = m_startPromise && !m_startPromise->future().isFinished();
    setState(State::Idle);
    if (starting)
        settleStart(error);
    else
        Q_EMIT stopped(error);
}

void ScreenMirrorClient::settleStart(const Result<void> &result)
{
    if (!m_startPromise || m_startPromise->future().isFinished())
        return;
    const std::shared_ptr<QPromise<Result<void>>> promise = std::exchange(m_startPromise, nullptr);
    promise->addResult(result);
    promise->finish();
}

void ScreenMirrorClient::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    Q_EMIT stateChanged();
}

void ScreenMirrorClient::setControl(Control control)
{
    if (m_control == control)
        return;
    m_control = control;
    Q_EMIT controlChanged();
}

void ScreenMirrorClient::setScreenSize(const QSize &size)
{
    if (m_screenSize == size)
        return;
    m_screenSize = size;
    Q_EMIT screenSizeChanged();
}

} // namespace qpm::core::service
