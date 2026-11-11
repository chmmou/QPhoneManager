#pragma once

#include <QString>

namespace qpm {

/**
 * Checks whether a service with the given well-known name, for example
 * "org.freedesktop.secrets", is registered on the D-Bus session bus.
 *
 * Returns false when no session bus is reachable. The call waits for the
 * answer of the bus, so it suits start-up checks, not code that runs often.
 */
[[nodiscard]] bool isSessionServiceRegistered(const QString &serviceName);

} // namespace qpm
