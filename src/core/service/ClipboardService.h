#pragma once

#include "core/Error.h"

#include <QFuture>
#include <QObject>
#include <QString>

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::service {

/**
 * The clipboard of the connected phone: writing text to it, reading it, and
 * keeping it the same as the clipboard of this computer while syncing is on.
 *
 * While syncing, the phone reports a new clip on its own while no OBEX
 * session runs; such a clip arrives through phoneTextChanged(). Text of this
 * computer goes to the phone through offerDesktopText(). A text that came
 * from one side is never sent back to it, so that one copy does not travel
 * back and forth. Only plain text of 1 to maxTextLength characters is
 * exchanged; anything longer from the phone is ignored and logged without
 * its content.
 *
 * Every call that needs the phone fails with ErrorCode::ConnectionClosed when
 * it is not connected. Lives in the main thread; no call blocks. Does not own
 * the connection, which must outlive it.
 */
class ClipboardService : public QObject
{
    Q_OBJECT

public:
    /// Longest clip exchanged, in characters.
    static constexpr qsizetype maxTextLength = 65536;

    explicit ClipboardService(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /**
     * Turns syncing on or off. Tells a connected phone at once whether to
     * report its clips; a later connection asks for that itself through
     * PhoneConnection::Options::clipboardWatch.
     */
    void setSyncing(bool syncing);
    [[nodiscard]] bool isSyncing() const { return m_syncing; }

    /**
     * Puts `text` on the clipboard of the phone. Fails with
     * ErrorCode::InvalidInput for an empty text and with
     * ErrorCode::LimitExceeded above maxTextLength.
     */
    QFuture<Result<void>> sendToPhone(const QString &text);

    /**
     * Reads the clipboard of the phone; empty when it holds no text. The
     * phone may answer only after it has brought its app to the front for a
     * moment, which Android requires for reading. Fails with
     * ErrorCode::Timeout when no answer comes within 25 seconds.
     */
    QFuture<Result<QString>> readFromPhone();

    /**
     * Hands the clipboard text of this computer over while syncing: sent to
     * the phone unless it is what was last exchanged with it. Does nothing
     * while not syncing or not connected.
     */
    void offerDesktopText(const QString &text);

Q_SIGNALS:
    /// The phone reported a clip that differs from the last one exchanged.
    void phoneTextChanged(const QString &text);

private:
    void onUnsolicited(const QString &line);
    void onStateChanged();

    protocol::PhoneConnection *m_connection = nullptr;
    /// The text last sent to the phone or received from it.
    QString m_lastExchanged;
    bool m_syncing = false;
};

} // namespace qpm::core::service
