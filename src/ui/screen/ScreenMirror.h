#pragma once

#include "core/Error.h"
#include "core/service/ScreenMirrorClient.h"

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace qpm::ui {

/**
 * The screen of the connected phone for the window: starting and ending the
 * mirroring, the newest picture, whether the phone takes input, and the
 * input itself.
 *
 * Pictures are decoded off the main thread, one at a time; a picture that
 * arrives while another is decoded replaces the one waiting, so the window
 * always catches up with the newest. A picture that cannot be decoded is
 * left out and logged, and the next one takes its place.
 *
 * Moves of a finger and typed text are combined while the phone has not
 * answered the one before, so a fast mouse or fast typing never piles up
 * commands. Every failure is reported once through errorOccurred() as a
 * translated text that says what to do; that the phone takes no input is
 * shown by `control` and `hintText` instead. Lives in the main thread; no
 * call blocks. The client must outlive the object.
 */
class ScreenMirror : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ScreenMirror)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged FINAL)
    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)
    /// The screen is shown and follows the phone.
    Q_PROPERTY(bool active READ isActive NOTIFY stateChanged FINAL)
    /// The mirroring is being started, the phone's question to its user included.
    Q_PROPERTY(bool starting READ isStarting NOTIFY stateChanged FINAL)
    /// A picture is there to show and to save.
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged FINAL)
    /// Size of the phone screen in pixels in its current orientation; empty without a picture.
    Q_PROPERTY(QSize frameSize READ frameSize NOTIFY frameChanged FINAL)
    Q_PROPERTY(Control control READ control NOTIFY controlChanged FINAL)
    /// Input is not known to fail; false while the phone takes no input.
    Q_PROPERTY(bool canControl READ canControl NOTIFY controlChanged FINAL)
    /// What the user has to do on the phone right now; empty when nothing.
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged FINAL)
    /// Why the phone takes no input and what helps; empty while it does.
    Q_PROPERTY(QString hintText READ hintText NOTIFY controlChanged FINAL)

public:
    enum class State { Off, Starting, WaitingForConsent, Connecting, Running };
    Q_ENUM(State)

    enum class Control { Unknown, Available, ServiceOff, NotSupported };
    Q_ENUM(Control)

    explicit ScreenMirror(core::service::ScreenMirrorClient *client, QObject *parent = nullptr);

    [[nodiscard]] bool isConnected() const { return m_connected; }
    [[nodiscard]] State state() const;
    [[nodiscard]] bool isActive() const { return state() == State::Running; }
    [[nodiscard]] bool isStarting() const;
    [[nodiscard]] bool hasFrame() const { return !m_frame.isNull(); }
    [[nodiscard]] QSize frameSize() const { return m_frame.size(); }
    [[nodiscard]] Control control() const;
    [[nodiscard]] bool canControl() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString hintText() const;

    /// The newest picture, cropped to the phone screen; null without one.
    [[nodiscard]] QImage frame() const { return m_frame; }

    /// Ends a running mirroring when the connection ends.
    void setConnected(bool connected);

    /// Starts the mirroring; the phone asks its user first.
    Q_INVOKABLE bool start();
    /// Ends the mirroring and forgets the picture.
    Q_INVOKABLE void stop();

    /// Taps the screen position `x`, `y` in phone pixels.
    Q_INVOKABLE bool tap(int x, int y);
    Q_INVOKABLE bool longPress(int x, int y);
    /// Swipes from one screen position to another.
    Q_INVOKABLE bool swipe(int fromX, int fromY, int toX, int toY);
    /// Scrolls at `x`, `y`; `up` shows what lies above.
    Q_INVOKABLE bool scroll(int x, int y, bool up);
    /**
     * Presses a key of the phone: "back", "home", "recents", "left",
     * "right", "up", "down", "enter" or "backspace". Returns false for any
     * other name.
     */
    Q_INVOKABLE bool key(const QString &name);
    /// Types `text` into the focused field of the phone; line breaks are left out.
    Q_INVOKABLE bool typeText(const QString &text);

    /// Opens the page on the phone that explains how to switch on its accessibility service.
    Q_INVOKABLE bool showAccessibilitySettings();
    /// Asks the phone again whether it takes input, after the user changed its setting.
    Q_INVOKABLE bool checkControl();

    /// Saves the newest picture as a PNG file `file`, which is replaced when it exists.
    Q_INVOKABLE bool saveScreenshot(const QUrl &file);

    /// Puts a finger down at `position` in phone pixels; see ScreenMirrorClient::pointer().
    void pressPointer(const QPoint &position);
    /// Moves the finger that is down; ignored while none is.
    void movePointer(const QPoint &position);
    void releasePointer(const QPoint &position);

    /// A translated text for a failure: what happened and what to do; empty for a cancellation.
    [[nodiscard]] static QString messageFor(const core::Error &error);
    /// The same for a failed start, which tells what to do on the phone.
    [[nodiscard]] static QString startMessageFor(const core::Error &error);

Q_SIGNALS:
    void connectedChanged();
    void stateChanged();
    void frameChanged();
    void controlChanged();
    void errorOccurred(const QString &message);
    void infoMessage(const QString &message);

private:
    void onFrame(const core::service::ScreenFrame &frame);
    void decode(const core::service::ScreenFrame &frame);
    void onStopped(const core::Error &error);
    void sendMove(const QPoint &position);
    void sendText(const QString &text);
    /// Reports a failed input once; a refusal for lack of control shows in hintText instead.
    void report(const core::Result<void> &result);
    void clearFrame();

    core::service::ScreenMirrorClient *m_client = nullptr;
    QImage m_frame;
    std::optional<core::service::ScreenFrame> m_waitingFrame;
    std::optional<QPoint> m_pendingMove;
    QString m_pendingText;
    quint64 m_generation = 0;
    bool m_connected = false;
    bool m_decoding = false;
    bool m_pointerDown = false;
    bool m_moveInFlight = false;
    bool m_textInFlight = false;
};

} // namespace qpm::ui
