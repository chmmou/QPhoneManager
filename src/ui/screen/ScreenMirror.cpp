#include "ui/screen/ScreenMirror.h"

#include "core/Logging.h"
#include "ui/contacts/PhoneImageProvider.h"

#include <QBuffer>
#include <QFileInfo>
#include <QHash>
#include <QImageWriter>
#include <QSaveFile>
#include <QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace qpm::ui {

namespace {

using core::Error;
using core::ErrorCode;
using core::Result;
using Client = core::service::ScreenMirrorClient;

// Decodes and crops one frame; runs on a worker thread and touches nothing shared.
Result<QImage> decodeFrame(const core::service::ScreenFrame &frame)
{
    QByteArray bytes = frame.image;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    const Result<QImage> read = readImageSafely(&buffer);
    if (!read.hasValue())
        return read.error();
    const QImage &image = read.value();
    if (image.width() < frame.screenSize.width() || image.height() < frame.screenSize.height()) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("Picture of %1 x %2 pixels is smaller than the screen")
                         .arg(image.width())
                         .arg(image.height()));
    }
    if (image.size() == frame.screenSize)
        return image;
    // The phone pads its rows; only the top left part shows the screen.
    return image.copy(QRect(QPoint(0, 0), frame.screenSize));
}

} // namespace

ScreenMirror::ScreenMirror(Client *client, QObject *parent) : QObject(parent), m_client(client)
{
    Q_ASSERT(client);
    connect(client, &Client::stateChanged, this, &ScreenMirror::stateChanged);
    connect(client, &Client::controlChanged, this, &ScreenMirror::controlChanged);
    connect(client, &Client::frameReceived, this, &ScreenMirror::onFrame);
    connect(client, &Client::stopped, this, &ScreenMirror::onStopped);
}

ScreenMirror::State ScreenMirror::state() const
{
    switch (m_client->state()) {
    case Client::State::Idle:
        return State::Off;
    case Client::State::Starting:
        return State::Starting;
    case Client::State::AwaitingConsent:
        return State::WaitingForConsent;
    case Client::State::Connecting:
        return State::Connecting;
    case Client::State::Running:
        break;
    }
    return State::Running;
}

bool ScreenMirror::isStarting() const
{
    const State current = state();
    return current == State::Starting || current == State::WaitingForConsent ||
           current == State::Connecting;
}

ScreenMirror::Control ScreenMirror::control() const
{
    switch (m_client->control()) {
    case Client::Control::Unknown:
        return Control::Unknown;
    case Client::Control::Available:
        return Control::Available;
    case Client::Control::ServiceOff:
        return Control::ServiceOff;
    case Client::Control::NotSupported:
        break;
    }
    return Control::NotSupported;
}

bool ScreenMirror::canControl() const
{
    const Control current = control();
    return m_connected && current != Control::ServiceOff && current != Control::NotSupported;
}

QString ScreenMirror::statusText() const
{
    switch (state()) {
    case State::Starting:
        return tr("Asking the phone for its screen.");
    case State::WaitingForConsent:
        return tr("Confirm on the phone that it may share its screen. Choose the entire screen "
                  "there.");
    case State::Connecting:
        return tr("The phone sends its screen.");
    case State::Off:
    case State::Running:
        break;
    }
    return {};
}

QString ScreenMirror::hintText() const
{
    if (!m_connected)
        return {};
    switch (control()) {
    case Control::ServiceOff:
        return tr("To control the phone from here, switch on the accessibility service of "
                  "MyPhoneExplorer Client on the phone.");
    case Control::NotSupported:
        return tr("This phone shows its screen but takes no input from here. That needs "
                  "Android 7 or newer.");
    case Control::Unknown:
    case Control::Available:
        break;
    }
    return {};
}

void ScreenMirror::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    if (!connected) {
        m_client->stop();
        clearFrame();
    }
    Q_EMIT connectedChanged();
    Q_EMIT controlChanged();
}

bool ScreenMirror::start()
{
    if (!m_connected || state() != State::Off)
        return false;
    clearFrame();
    m_client->start().then(this, [this](const Result<void> &started) {
        if (started.hasValue())
            return;
        if (const QString message = startMessageFor(started.error()); !message.isEmpty())
            Q_EMIT errorOccurred(message);
    });
    return true;
}

void ScreenMirror::stop()
{
    m_client->stop();
    clearFrame();
}

bool ScreenMirror::tap(int x, int y)
{
    if (!m_connected)
        return false;
    pressPointer(QPoint(x, y));
    releasePointer(QPoint(x, y));
    return true;
}

bool ScreenMirror::longPress(int x, int y)
{
    if (!m_connected)
        return false;
    m_client->longPress(QPoint(x, y)).then(this, [this](const Result<void> &done) {
        report(done);
    });
    return true;
}

bool ScreenMirror::swipe(int fromX, int fromY, int toX, int toY)
{
    if (!m_connected)
        return false;
    pressPointer(QPoint(fromX, fromY));
    movePointer(QPoint(toX, toY));
    releasePointer(QPoint(toX, toY));
    return true;
}

bool ScreenMirror::scroll(int x, int y, bool up)
{
    if (!m_connected)
        return false;
    m_client->scroll(QPoint(x, y), up ? Client::Scroll::Up : Client::Scroll::Down)
        .then(this, [this](const Result<void> &done) { report(done); });
    return true;
}

bool ScreenMirror::key(const QString &name)
{
    static const QHash<QString, Client::Key> keys = {
        {QStringLiteral("back"), Client::Key::Back},
        {QStringLiteral("home"), Client::Key::Home},
        {QStringLiteral("recents"), Client::Key::Recents},
        {QStringLiteral("left"), Client::Key::Left},
        {QStringLiteral("right"), Client::Key::Right},
        {QStringLiteral("up"), Client::Key::Up},
        {QStringLiteral("down"), Client::Key::Down},
        {QStringLiteral("enter"), Client::Key::Enter},
        {QStringLiteral("backspace"), Client::Key::Backspace},
    };
    const auto found = keys.constFind(name);
    if (!m_connected || found == keys.cend())
        return false;
    m_client->pressKey(found.value()).then(this, [this](const Result<void> &done) {
        report(done);
    });
    return true;
}

bool ScreenMirror::typeText(const QString &text)
{
    QString printable;
    printable.reserve(text.size());
    for (const QChar c : text) {
        if (c.unicode() >= u' ' && c.unicode() != 0x7F)
            printable.append(c);
    }
    if (!m_connected || printable.isEmpty())
        return false;
    if (m_textInFlight) {
        m_pendingText += printable;
        return true;
    }
    sendText(printable);
    return true;
}

void ScreenMirror::sendText(const QString &text)
{
    // A surrogate pair stays whole across the cut.
    qsizetype cut = std::min(text.size(), Client::maxTextLength);
    if (cut < text.size() && text.at(cut - 1).isHighSurrogate())
        --cut;
    m_pendingText = text.mid(cut) + m_pendingText;
    m_textInFlight = true;
    const quint64 generation = m_generation;
    m_client->typeText(text.left(cut)).then(this, [this, generation](const Result<void> &done) {
        m_textInFlight = false;
        report(done);
        if (generation != m_generation || !done.hasValue()) {
            m_pendingText.clear();
            return;
        }
        if (!m_pendingText.isEmpty()) {
            const QString next = std::exchange(m_pendingText, QString());
            sendText(next);
        }
    });
}

bool ScreenMirror::showAccessibilitySettings()
{
    if (!m_connected)
        return false;
    m_client->showAccessibilitySettings().then(this, [this](const Result<void> &shown) {
        if (shown.hasValue()) {
            Q_EMIT infoMessage(tr("The phone shows how to switch on the service. Come back "
                                  "here afterwards and check again."));
        } else if (const QString message = messageFor(shown.error()); !message.isEmpty()) {
            Q_EMIT errorOccurred(message);
        }
    });
    return true;
}

bool ScreenMirror::checkControl()
{
    if (!m_connected)
        return false;
    m_client->checkControl().then(this, [this](const Result<Client::Control> &checked) {
        if (!checked.hasValue()) {
            if (const QString message = messageFor(checked.error()); !message.isEmpty())
                Q_EMIT errorOccurred(message);
        } else if (checked.value() == Client::Control::Available) {
            Q_EMIT infoMessage(tr("The phone can be controlled from here now."));
        }
    });
    return true;
}

bool ScreenMirror::saveScreenshot(const QUrl &file)
{
    if (m_frame.isNull()) {
        Q_EMIT errorOccurred(tr("There is no picture of the screen yet. Start the mirroring "
                                "first."));
        return false;
    }
    const QString local = file.toLocalFile();
    if (local.isEmpty() || !QFileInfo(QFileInfo(local).absolutePath()).isDir()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    const QImage image = m_frame;
    QtConcurrent::run([image, local]() {
        QSaveFile target(local);
        if (!target.open(QIODevice::WriteOnly))
            return false;
        QImageWriter writer(&target, "png");
        return writer.write(image) && target.commit();
    }).then(this, [this, local](bool saved) {
        if (saved) {
            Q_EMIT infoMessage(
                tr("The screenshot is saved as %1.").arg(QFileInfo(local).fileName()));
        } else {
            Q_EMIT errorOccurred(tr("The screenshot cannot be saved. Check the folder and the free "
                                    "space."));
        }
    });
    return true;
}

void ScreenMirror::pressPointer(const QPoint &position)
{
    if (!m_connected)
        return;
    m_pointerDown = true;
    m_pendingMove.reset();
    m_client->pointer(Client::Pointer::Down, position).then(this, [this](const Result<void> &done) {
        report(done);
    });
}

void ScreenMirror::movePointer(const QPoint &position)
{
    if (!m_pointerDown)
        return;
    if (m_moveInFlight) {
        m_pendingMove = position;
        return;
    }
    sendMove(position);
}

void ScreenMirror::sendMove(const QPoint &position)
{
    m_moveInFlight = true;
    m_client->pointer(Client::Pointer::Move, position).then(this, [this](const Result<void> &done) {
        m_moveInFlight = false;
        report(done);
        if (m_pendingMove && m_pointerDown) {
            const QPoint next = *m_pendingMove;
            m_pendingMove.reset();
            sendMove(next);
        }
    });
}

void ScreenMirror::releasePointer(const QPoint &position)
{
    if (!m_pointerDown)
        return;
    m_pointerDown = false;
    // The lift carries the last position, so a waiting move adds nothing.
    m_pendingMove.reset();
    m_client->pointer(Client::Pointer::Up, position).then(this, [this](const Result<void> &done) {
        report(done);
    });
}

QString ScreenMirror::messageFor(const Error &error)
{
    switch (error.code()) {
    case ErrorCode::None:
    case ErrorCode::Cancelled:
        return {};
    case ErrorCode::ConnectionClosed:
        return tr("The connection to the phone ended. Connect again and try again.");
    case ErrorCode::Timeout:
        return tr("The phone did not answer in time. Try again.");
    case ErrorCode::PermissionDenied:
        return tr("The phone takes no input from here. Switch on the accessibility service of "
                  "MyPhoneExplorer Client on the phone.");
    case ErrorCode::NotSupported:
        return tr("This phone takes no input from here. That needs Android 7 or newer.");
    case ErrorCode::LimitExceeded:
        return tr("The phone sent a larger picture than this program reads.");
    case ErrorCode::ProtocolError:
        return tr("The phone sent its screen in a form this program cannot read.");
    case ErrorCode::InvalidInput:
        return tr("That position is outside the phone screen.");
    case ErrorCode::Rejected:
    case ErrorCode::AccessDenied:
    case ErrorCode::WrongPin:
    case ErrorCode::NotFound:
    case ErrorCode::AlreadyConnected:
    case ErrorCode::BluetoothServiceNotFound:
    case ErrorCode::WifiDisabled:
    case ErrorCode::IoFailure:
        break;
    }
    return tr("The phone could not do this. Try again.");
}

QString ScreenMirror::startMessageFor(const Error &error)
{
    switch (error.code()) {
    case ErrorCode::Rejected:
        return tr("The phone did not share its screen. Start again and allow it on the phone.");
    case ErrorCode::Timeout:
        return tr("Nobody confirmed the sharing on the phone in time. Start again and confirm it "
                  "there.");
    case ErrorCode::NotSupported:
        return tr("This phone cannot share its screen here. That needs Android 5 or newer and a "
                  "connection over Wi-Fi or USB.");
    case ErrorCode::ConnectionClosed:
        return tr("The phone did not send its screen. Start the mirroring again.");
    default:
        break;
    }
    return messageFor(error);
}

void ScreenMirror::onFrame(const core::service::ScreenFrame &frame)
{
    if (m_decoding) {
        m_waitingFrame = frame;
        return;
    }
    decode(frame);
}

void ScreenMirror::decode(const core::service::ScreenFrame &frame)
{
    m_decoding = true;
    const quint64 generation = m_generation;
    QtConcurrent::run(&decodeFrame, frame)
        .then(this, [this, generation](const Result<QImage> &decoded) {
            m_decoding = false;
            if (generation != m_generation)
                return;
            if (decoded.hasValue()) {
                m_frame = decoded.value();
                Q_EMIT frameChanged();
            } else {
                // One broken picture is skipped; the next one shows the screen again.
                qCWarning(core::lcUi) << "Screen picture left out:" << decoded.error().message();
            }
            if (m_waitingFrame) {
                const core::service::ScreenFrame next = std::move(*m_waitingFrame);
                m_waitingFrame.reset();
                decode(next);
            }
        });
}

void ScreenMirror::onStopped(const Error &error)
{
    clearFrame();
    // A connection that ended is reported by the connection itself.
    if (!m_connected)
        return;
    if (error.code() == ErrorCode::ConnectionClosed) {
        Q_EMIT errorOccurred(tr("The picture of the screen broke off. Start the mirroring again."));
        return;
    }
    if (const QString message = messageFor(error); !message.isEmpty())
        Q_EMIT errorOccurred(message);
}

void ScreenMirror::report(const Result<void> &result)
{
    // A connection that ended is reported by the connection itself.
    if (result.hasValue() || !m_connected)
        return;
    const ErrorCode code = result.error().code();
    // That the phone takes no input shows on the page, once, instead of on every key.
    if (code == ErrorCode::PermissionDenied || code == ErrorCode::NotSupported)
        return;
    if (const QString message = messageFor(result.error()); !message.isEmpty())
        Q_EMIT errorOccurred(message);
}

void ScreenMirror::clearFrame()
{
    ++m_generation;
    m_waitingFrame.reset();
    m_pendingMove.reset();
    m_pendingText.clear();
    m_pointerDown = false;
    if (m_frame.isNull())
        return;
    m_frame = QImage();
    Q_EMIT frameChanged();
}

} // namespace qpm::ui
