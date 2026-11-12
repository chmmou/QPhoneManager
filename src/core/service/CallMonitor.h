#pragma once

#include "core/Error.h"

#include <QFuture>
#include <QObject>
#include <QString>

#include <memory>

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::store {
class ContactStore;
}

namespace qpm::core::service {

/**
 * The call on the connected phone as the phone reports it: ringing, being
 * dialled, going on, over. Answers, ends, mutes and switches the speaker of
 * that call.
 *
 * The phone reports calls only while no OBEX session runs and once the
 * connection asked for it, which it does in its handshake. The number of an
 * incoming call can be missing when the phone app may not read the call log,
 * and an outgoing call may show only once it goes on, without its number.
 * The name is looked up among the contacts of the store set with
 * setContactStore(). A report that cannot be read is ignored and logged
 * without its content; a number is taken only in the form of digits, an
 * optional leading +, * and #, with at most maxNumberLength characters.
 *
 * Every call that needs the phone fails with ErrorCode::ConnectionClosed when
 * it is not connected, and answer() with ErrorCode::InvalidInput while no
 * call rings. When the connection ends, the state goes back to Idle. Lives in
 * the main thread; no call blocks. Does not own the connection, which must
 * outlive it.
 */
class CallMonitor : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Ringing, Dialling, Active };
    Q_ENUM(State)

    /// Longest number taken from the phone.
    static constexpr qsizetype maxNumberLength = 32;

    explicit CallMonitor(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /**
     * The contacts to look names up in; null for none. Only a weak
     * reference is kept.
     */
    void setContactStore(const std::shared_ptr<store::ContactStore> &store);

    [[nodiscard]] State state() const { return m_state; }
    /// The number of the other side; empty when the phone did not send it.
    [[nodiscard]] QString number() const { return m_number; }
    /// The name of the contact with that number; empty when none has it.
    [[nodiscard]] QString name() const { return m_name; }
    /// The microphone is off, as last set through setMuted().
    [[nodiscard]] bool isMuted() const { return m_muted; }
    /// The speaker is on, as last set through setSpeaker().
    [[nodiscard]] bool isSpeakerOn() const { return m_speaker; }

    /// Takes the ringing call.
    QFuture<Result<void>> answer();
    /**
     * Ends the call or turns a ringing one down. The command goes out in
     * every state, Idle included, for a call the phone did not report.
     */
    QFuture<Result<void>> hangUp();
    QFuture<Result<void>> setMuted(bool muted);
    QFuture<Result<void>> setSpeaker(bool on);

Q_SIGNALS:
    /// State, number or name changed.
    void callChanged();
    void mutedChanged();
    void speakerChanged();

private:
    void onUnsolicited(const QString &line);
    void onStateChanged();
    void setCall(State state, const QString &number);
    [[nodiscard]] QString lookUpName(const QString &number) const;
    QFuture<Result<void>> sendSwitch(const QString &command, bool on, bool CallMonitor::*target,
                                     void (CallMonitor::*changed)());

    protocol::PhoneConnection *m_connection = nullptr;
    std::weak_ptr<store::ContactStore> m_contacts;
    State m_state = State::Idle;
    QString m_number;
    QString m_name;
    bool m_muted = false;
    bool m_speaker = false;
};

} // namespace qpm::core::service
