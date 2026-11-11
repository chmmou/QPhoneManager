#include "ui/QrCodeProvider.h"

#include <QNetworkInterface>
#include <QRandomGenerator>

namespace qpm::ui {

namespace {

constexpr int sessionIdLength = 20;

bool isPrivate(const QHostAddress &address)
{
    return address.isInSubnet(QHostAddress(QStringLiteral("10.0.0.0")), 8) ||
           address.isInSubnet(QHostAddress(QStringLiteral("172.16.0.0")), 12) ||
           address.isInSubnet(QHostAddress(QStringLiteral("192.168.0.0")), 16);
}

} // namespace

QrCodeProvider::QrCodeProvider(QObject *parent) : QObject(parent) {}

void QrCodeProvider::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    if (active) {
        m_ownAddress = chooseOwnAddress(localAddresses());
        m_text = m_ownAddress.isNull() ? QString() : codeText(newSessionId(), m_ownAddress);
    } else {
        m_ownAddress.clear();
        m_text.clear();
    }
    Q_EMIT textChanged();
    Q_EMIT activeChanged();
}

QString QrCodeProvider::ownAddress() const
{
    return m_ownAddress.isNull() ? QString() : m_ownAddress.toString();
}

QString QrCodeProvider::newSessionId()
{
    static constexpr QLatin1StringView alphabet(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    QString id;
    id.reserve(sessionIdLength);
    QRandomGenerator *random = QRandomGenerator::system();
    for (int i = 0; i < sessionIdLength; ++i)
        id.append(alphabet.at(random->bounded(int(alphabet.size()))));
    return id;
}

QHostAddress QrCodeProvider::chooseOwnAddress(const QList<QHostAddress> &candidates)
{
    QHostAddress fallback;
    for (const QHostAddress &address : candidates) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback() ||
            address.isLinkLocal()) {
            continue;
        }
        if (isPrivate(address))
            return address;
        if (fallback.isNull())
            fallback = address;
    }
    return fallback;
}

QList<QHostAddress> QrCodeProvider::localAddresses()
{
    QList<QHostAddress> addresses;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        const QNetworkInterface::InterfaceFlags flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        const QList<QNetworkAddressEntry> entries = interface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                addresses.append(entry.ip());
        }
    }
    return addresses;
}

QString QrCodeProvider::codeText(const QString &sessionId, const QHostAddress &ownAddress)
{
    // The client accepts a code only with exactly this prefix. It reads the
    // address and connects to this computer itself; the address of the
    // maker's server in the prefix is never contacted by this program.
    return QStringLiteral("https://www.fjsoft.at/mpeclient.php?sid=%1&ownip=%2")
        .arg(sessionId, ownAddress.toString());
}

} // namespace qpm::ui
