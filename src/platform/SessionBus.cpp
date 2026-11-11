#include "platform/SessionBus.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>

namespace qpm {

bool isSessionServiceRegistered(const QString &serviceName)
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }

    const QDBusConnectionInterface *busInterface = bus.interface();
    if (busInterface == nullptr) {
        return false;
    }

    const QDBusReply<bool> reply = busInterface->isServiceRegistered(serviceName);
    return reply.isValid() && reply.value();
}

} // namespace qpm
