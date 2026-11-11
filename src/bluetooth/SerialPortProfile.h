#pragma once

#include <QBluetoothUuid>

namespace qpm {

/**
 * Returns the service UUID of the Serial Port Profile,
 * 00001101-0000-1000-8000-00805F9B34FB. It is the only RFCOMM service that
 * MyPhoneExplorer Client offers over Bluetooth.
 */
[[nodiscard]] QBluetoothUuid serialPortProfileUuid();

} // namespace qpm
