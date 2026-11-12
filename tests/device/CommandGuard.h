#pragma once

#include "core/protocol/Transport.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace qpm::test {

/**
 * A transport for tests against a real phone that refuses to pass on anything
 * that sends a text message or places or answers a call: ATD, ATA, every
 * AT+CMGS= except the probe AT+CMGS=?, AT*NOTIACTION, an OBEX
 * PUT whose name contains "/msg" other than telecom/msg/setflags, since the
 * phone hands such a PUT to its message handler, and an OBEX PUT it cannot
 * decode. Of the app commands only three forms pass: AT*EJAVA=1 for the
 * list, and AT*EJAVA=4,<package> and AT*EJAVA=2,<package> with a valid
 * package name to start an app and to ask for its removal; every other
 * AT*EJAVA is refused. Each line of a frame is checked after trimming it the way the phone
 * does, without regard to case. Hanging up with ATH or AT+CHUP passes.
 *
 * The commands that capture or control the screen of the phone,
 * AT*STARTCAPTURESERVER, AT*C, AT*K, AT*T, AT*SELTEXT, AT*KBRD and
 * AT*SHOWACCSETTINGS, probes included, pass only when the guard was made for
 * an emulator: on someone's own phone the capture asks its owner, and input
 * acts on whatever the phone shows.
 *
 * For the same reason AT*NOTIC in every form, which dismisses notifications,
 * and AT*CLIPBOARD=, which replaces the clipboard, pass only to an emulator.
 *
 * Two exceptions come from the environment. AT+CMGS=<number>,<text>,... and
 * AT+CMGS=<number>,,<flags> pass when <number> is the one in QPM_TEST_SMS_TO,
 * and ATD<number>; or ATD<number>;,<sim> passes when <number> is the one in
 * QPM_TEST_CALL_TO. Spaces do not count, and +49 equals a leading 0. A value
 * that is not a full number, with a leading 0 or a + and at least eight
 * characters, counts as unset. While a variable is unset, its commands stay
 * refused. Neither number ever appears in a refusal reason or in the log.
 *
 * A refused frame is not written. The guard records the reason, reports
 * ErrorCode::Rejected through failed() and closes the connection, so the
 * test fails before anything reaches the phone. Everything else is passed on
 * unchanged. The guard owns the wrapped transport.
 */
class CommandGuard : public qpm::core::protocol::Transport
{
    Q_OBJECT

public:
    /// The device behind the guard. Anything unknown counts as a phone.
    enum class Target { Phone, Emulator };

    explicit CommandGuard(std::unique_ptr<qpm::core::protocol::Transport> inner,
                          Target target = Target::Phone, QObject *parent = nullptr);

    void open() override;
    void close() override;
    void write(const QByteArray &frame) override;
    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] QString peerDescription() const override;

    /// The reasons of all refused frames, in order.
    [[nodiscard]] QStringList blocked() const { return m_blocked; }

    /**
     * Returns why the AT command `command` must not reach `target`, or
     * nothing when it may. Reads the allowed numbers from the environment on
     * each call.
     */
    [[nodiscard]] static std::optional<QString> blockReason(const QString &command,
                                                            Target target = Target::Phone);

    /**
     * Same for a frame as written to the transport: one or more AT lines or
     * an OBEX packet.
     */
    [[nodiscard]] static std::optional<QString> blockReasonForFrame(const QByteArray &frame,
                                                                    Target target = Target::Phone);

private:
    std::unique_ptr<qpm::core::protocol::Transport> m_inner;
    QStringList m_blocked;
    Target m_target = Target::Phone;
};

} // namespace qpm::test
