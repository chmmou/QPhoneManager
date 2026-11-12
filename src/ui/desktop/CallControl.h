#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::core {
class Error;
}

namespace qpm::core::service {
class CallMonitor;
}

namespace qpm::ui {

/**
 * The call on the connected phone for the call window: who calls, what goes
 * on, and answering, ending, muting and the speaker.
 *
 * Every failure is reported once through errorOccurred() as a translated text
 * that says what to do; an action that cannot start returns false. Lives in
 * the main thread; no call blocks. The monitor must outlive the object.
 */
class CallControl : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CallControl)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(State state READ state NOTIFY callChanged FINAL)
    /// A call rings, is dialled or goes on; the call window shows while this holds.
    Q_PROPERTY(bool active READ isActive NOTIFY callChanged FINAL)
    /// The name of the caller, else the number, else a word for an unknown caller.
    Q_PROPERTY(QString callerText READ callerText NOTIFY callChanged FINAL)
    /// The number below a known name; empty otherwise.
    Q_PROPERTY(QString numberText READ numberText NOTIFY callChanged FINAL)
    /// What happens with the call, in a few words.
    Q_PROPERTY(QString statusText READ statusText NOTIFY callChanged FINAL)
    Q_PROPERTY(bool muted READ isMuted NOTIFY mutedChanged FINAL)
    Q_PROPERTY(bool speakerOn READ isSpeakerOn NOTIFY speakerChanged FINAL)

public:
    enum class State { Idle, Ringing, Dialling, Active };
    Q_ENUM(State)

    explicit CallControl(core::service::CallMonitor *monitor, QObject *parent = nullptr);

    [[nodiscard]] State state() const;
    [[nodiscard]] bool isActive() const { return state() != State::Idle; }
    [[nodiscard]] QString callerText() const;
    [[nodiscard]] QString numberText() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] bool isMuted() const;
    [[nodiscard]] bool isSpeakerOn() const;

    /// Takes the ringing call on the phone.
    Q_INVOKABLE bool answer();
    /// Ends the call, or turns a ringing one down.
    Q_INVOKABLE bool hangUp();
    Q_INVOKABLE bool setMuted(bool muted);
    Q_INVOKABLE bool setSpeakerOn(bool on);

Q_SIGNALS:
    void callChanged();
    void mutedChanged();
    void speakerChanged();
    void errorOccurred(const QString &message);

private:
    [[nodiscard]] QString messageFor(const core::Error &error) const;

    core::service::CallMonitor *m_monitor = nullptr;
};

} // namespace qpm::ui
