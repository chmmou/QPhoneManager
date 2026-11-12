#pragma once

#include "core/Error.h"
#include "core/codec/Limits.h"
#include "core/service/FileService.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QPromise>
#include <QSize>
#include <QString>

#include <chrono>
#include <memory>
#include <optional>

class QTcpSocket;
class QTimer;

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::service {

/// One picture of the phone screen as the phone sends it, not decoded yet.
struct ScreenFrame
{
    enum class Format { Jpeg, Png };

    /// Size of the phone display in its current orientation; input positions refer to it.
    QSize screenSize;
    /// Turn of the phone display in quarter turns, 0 to 3.
    int rotation = 0;
    /**
     * Size of the encoded picture. It is at least screenSize and can be
     * wider, where the phone padded its rows; only the part of screenSize
     * at the top left shows the screen.
     */
    QSize imageSize;
    Format format = Format::Jpeg;
    /// The encoded picture; untrusted, and decoded only with the limits for pictures from the
    /// phone.
    QByteArray image;
};

/**
 * Reads the stream in which the phone sends its screen: per picture
 * "FileBegin;<w>x<h>;<rotation>;<w>x<h>;<JPG|PNG>;<quality>" and a line end,
 * the picture, and "\r\nFileEnd". Pictures carry no length, so one ends
 * where "\r\nFileEnd" follows the end marker of its format.
 *
 * A picture above maxImageBytes, a header line above maxHeaderBytes, sizes
 * of 0 or above limits::maxImageWidth or limits::maxImageHeight, a picture
 * smaller than the screen it shows, and anything else out of form fail with
 * ErrorCode::LimitExceeded or ErrorCode::ProtocolError, after which the
 * reader takes nothing more.
 */
class ScreenFrameReader
{
public:
    /// Largest encoded picture taken for one frame.
    static constexpr qsizetype maxImageBytes = 32 * limits::mebibyte;
    /// Longest header line of a frame.
    static constexpr qsizetype maxHeaderBytes = 256;

    /// Takes the next bytes of the stream.
    [[nodiscard]] Result<void> feed(const QByteArray &bytes);
    /// Hands over the frames completed since the last call, oldest first.
    [[nodiscard]] QList<ScreenFrame> takeFrames();

private:
    [[nodiscard]] Result<void> step(bool &progressed);
    [[nodiscard]] static Result<ScreenFrame> parseHeader(const QByteArray &line);

    QByteArray m_buffer;
    QList<ScreenFrame> m_frames;
    ScreenFrame m_current;
    qsizetype m_searchFrom = 0;
    bool m_inImage = false;
    Error m_error;
};

/**
 * Shows the screen of the connected phone and controls it: the phone asks
 * its user to allow the capture, then sends its screen over its own TCP
 * channel, and takes taps, swipes, keys and text over the AT session.
 *
 * Control needs the accessibility service of the phone app. Without it the
 * phone still shows its screen, but key and text commands fail with
 * ErrorCode::PermissionDenied, and taps are taken and silently dropped by
 * the phone; checkControl() tells beforehand, and showAccessibilitySettings()
 * opens the page on the phone where the user switches the service on.
 *
 * The screen channel needs the network or adb forwarding; over a transport
 * without side channels start() fails with ErrorCode::NotSupported. A stream
 * that breaks, a phone that ends it, data out of form and the end of the
 * connection stop the mirroring with stopped(). Frames come out at most
 * every minFrameInterval; frames that arrive faster are dropped, except the
 * newest, which follows once the interval is over.
 *
 * Every input call fails with ErrorCode::ConnectionClosed when the phone is
 * not connected, and with ErrorCode::InvalidInput for a position outside the
 * screen or unusable text before anything is sent. Lives in the main thread;
 * no call blocks. Does not own the connection, which must outlive it.
 */
class ScreenMirrorClient : public QObject
{
    Q_OBJECT

public:
    /// Port of the screen channel as the phone opens it.
    static constexpr quint16 screenPort = 5213;
    /// How long the phone may take to answer the start: it waits 19 seconds for its user.
    static constexpr std::chrono::seconds consentTimeout{25};
    /// Shortest time between two frames handed on.
    static constexpr std::chrono::milliseconds minFrameInterval{33};
    /// JPEG quality asked of the phone, 5 to 100.
    static constexpr int pictureQuality = 70;
    /// Most characters of one text input.
    static constexpr qsizetype maxTextLength = 1000;

    enum class State {
        Idle,
        /// Asking the phone whether it can capture its screen.
        Starting,
        /// The phone asks its user to allow the capture.
        AwaitingConsent,
        /// The user allowed it; the screen channel is being opened.
        Connecting,
        Running,
    };
    Q_ENUM(State)

    /// Whether the phone takes input.
    enum class Control {
        Unknown,
        Available,
        /// The accessibility service of the phone app is off.
        ServiceOff,
        /// The phone runs an Android version older than 7.
        NotSupported,
    };
    Q_ENUM(Control)

    enum class Key { Back, Home, Recents, Left, Right, Up, Down, Enter, Backspace };
    Q_ENUM(Key)

    /// A finger put down, moved or lifted; the phone plays the path when it is lifted.
    enum class Pointer { Down, Move, Up };
    Q_ENUM(Pointer)

    /// Up shows what lies above, as turning a mouse wheel away from the user does.
    enum class Scroll { Up, Down };
    Q_ENUM(Scroll)

    explicit ScreenMirrorClient(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /// See FileService::setEndpointResolver().
    void setEndpointResolver(FileService::EndpointResolver resolver);

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] Control control() const { return m_control; }
    /// The display of the phone in its current orientation; empty while unknown.
    [[nodiscard]] QSize screenSize() const { return m_screenSize; }

    /**
     * Makes the phone ask its user to allow the capture, and opens the
     * screen channel once allowed. The future ends when the channel is open.
     * Fails with ErrorCode::Rejected when the user declined, with
     * ErrorCode::Timeout when nobody answered on the phone, with
     * ErrorCode::NotSupported when the phone or the connection cannot
     * capture, with ErrorCode::ConnectionClosed when the channel cannot be
     * reached, and with ErrorCode::InvalidInput while a mirroring runs.
     */
    QFuture<Result<void>> start();

    /// Ends the mirroring; the phone stops capturing when its channel closes. No stopped() follows.
    void stop();

    /// Asks the phone whether it takes input, and updates control().
    QFuture<Result<Control>> checkControl();

    /// Opens the page on the phone that explains how to switch on the accessibility service.
    QFuture<Result<void>> showAccessibilitySettings();

    /// Puts a finger down at `position`, moves it there or lifts it there.
    QFuture<Result<void>> pointer(Pointer action, const QPoint &position);
    QFuture<Result<void>> longPress(const QPoint &position);
    /// Swipes a quarter of the screen height from `position`.
    QFuture<Result<void>> scroll(const QPoint &position, Scroll direction);
    QFuture<Result<void>> pressKey(Key key);
    /**
     * Types `text` into the focused field of the phone. Fails with
     * ErrorCode::InvalidInput for empty text, text above maxTextLength
     * characters and control characters such as a line break.
     */
    QFuture<Result<void>> typeText(const QString &text);

    /// The AT command for `key`, such as "AT*K=back".
    [[nodiscard]] static QString keyCommand(Key key);

Q_SIGNALS:
    void stateChanged();
    void controlChanged();
    void screenSizeChanged();
    void frameReceived(const qpm::core::service::ScreenFrame &frame);
    /// The mirroring ended without stop(): the stream broke or was out of form, or the connection
    /// ended.
    void stopped(const qpm::core::Error &error);

private:
    void setState(State state);
    void setControl(Control control);
    void setScreenSize(const QSize &size);
    void openChannel(const protocol::Endpoint &endpoint, quint64 generation);
    void connectSocket(const protocol::Endpoint &endpoint, quint64 generation, int triesLeft);
    void onSocketData();
    void onFrames(QList<ScreenFrame> frames);
    void deliverPendingFrame();
    void closeChannel();
    void fail(const Error &error);
    /// Settles the running start() with `result`, once.
    void settleStart(const Result<void> &result);
    [[nodiscard]] std::optional<protocol::Endpoint> endpoint(quint16 port) const;
    [[nodiscard]] Error checkPosition(const QPoint &position) const;
    QFuture<Result<void>> sendInput(const QString &command, const QString &what);

    protocol::PhoneConnection *m_connection = nullptr;
    FileService::EndpointResolver m_resolver;
    QTcpSocket *m_socket = nullptr;
    QTimer *m_frameTimer = nullptr;
    ScreenFrameReader m_reader;
    std::optional<ScreenFrame> m_pendingFrame;
    std::shared_ptr<QPromise<Result<void>>> m_startPromise;
    QSize m_screenSize;
    quint64 m_generation = 0;
    State m_state = State::Idle;
    Control m_control = Control::Unknown;
    bool m_received = false;
};

} // namespace qpm::core::service
