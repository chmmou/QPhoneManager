#include "bluetooth/SerialPortProfile.h"

namespace qpm {

QBluetoothUuid serialPortProfileUuid()
{
    return QBluetoothUuid(QBluetoothUuid::ServiceClassUuid::SerialPort);
}

} // namespace qpm
