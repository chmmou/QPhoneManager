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
 * that sends a text message or places, answers or ends a call: ATD, ATA, ATH,
 * AT+CHUP, every AT+CMGS= except the probe AT+CMGS=?, AT*NOTIACTION, and an
 * OBEX PUT below telecom/msg/ other than the setflags switch, which would
 * schedule a message.
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
    explicit CommandGuard(std::unique_ptr<qpm::core::protocol::Transport> inner,
                          QObject *parent = nullptr);

    void open() override;
    void close() override;
    void write(const QByteArray &frame) override;
    [[nodiscard]] bool isOpen() const override;
    [[nodiscard]] QString peerDescription() const override;

    /// The reasons of all refused frames, in order.
    [[nodiscard]] QStringList blocked() const { return m_blocked; }

    /// Returns why `command` must not reach a phone, or nothing when it may.
    [[nodiscard]] static std::optional<QString> blockReason(const QString &command);

    /// Same for a frame as written to the transport: an AT line or an OBEX packet.
    [[nodiscard]] static std::optional<QString> blockReasonForFrame(const QByteArray &frame);

private:
    std::unique_ptr<qpm::core::protocol::Transport> m_inner;
    QStringList m_blocked;
};

} // namespace qpm::test
