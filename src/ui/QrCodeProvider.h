#pragma once

#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * The content of the QR code that MyPhoneExplorer Client scans to connect to
 * this computer: the fixed prefix the client accepts, a fresh random session
 * id and the address of this computer in the local network.
 *
 * While `active` is true, AppController listens for the phone; each
 * activation draws a new session id. The session id comes from the system's
 * cryptographic random source. Nothing is sent to any server: the client
 * itself connects to the address in the code.
 */
class QrCodeProvider : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(QrCodeProvider)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged FINAL)
    /// The text to encode; empty while inactive or without a network address.
    Q_PROPERTY(QString text READ text NOTIFY textChanged FINAL)
    /// The address of this computer the phone connects to.
    Q_PROPERTY(QString ownAddress READ ownAddress NOTIFY textChanged FINAL)

public:
    explicit QrCodeProvider(QObject *parent = nullptr);

    [[nodiscard]] bool isActive() const { return m_active; }
    void setActive(bool active);
    [[nodiscard]] QString text() const { return m_text; }
    [[nodiscard]] QString ownAddress() const;
    [[nodiscard]] QHostAddress ownHostAddress() const { return m_ownAddress; }

    /// Twenty letters and digits from the system's cryptographic random source.
    [[nodiscard]] static QString newSessionId();

    /**
     * The address the phone should connect to: the first private IPv4
     * address (10/8, 172.16/12, 192.168/16) among `candidates`, else the first
     * other IPv4 address that is neither loopback nor link-local. A null
     * address when there is none.
     */
    [[nodiscard]] static QHostAddress chooseOwnAddress(const QList<QHostAddress> &candidates);

    /// The IPv4 addresses of the network interfaces that are up.
    [[nodiscard]] static QList<QHostAddress> localAddresses();

    /// The QR text for `sessionId` and `ownAddress`.
    [[nodiscard]] static QString codeText(const QString &sessionId, const QHostAddress &ownAddress);

Q_SIGNALS:
    void activeChanged();
    void textChanged();

private:
    QString m_text;
    QHostAddress m_ownAddress;
    bool m_active = false;
};

} // namespace qpm::ui
