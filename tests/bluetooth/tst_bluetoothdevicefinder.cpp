#include "bluetooth/BluetoothDeviceFinder.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QTest>

using qpm::BluetoothDeviceFinder;

class TestBluetoothDeviceFinder : public QObject
{
    Q_OBJECT

public:
    explicit TestBluetoothDeviceFinder(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void onlyPhonesAreListed_data();
    void onlyPhonesAreListed();
};

void TestBluetoothDeviceFinder::onlyPhonesAreListed_data()
{
    QTest::addColumn<quint32>("classOfDevice");
    QTest::addColumn<bool>("phone");

    // Class of device as the Bluetooth assigned numbers define it: the major
    // class sits in bits 8 to 12.
    QTest::newRow("smartphone") << quint32(0x5A020C) << true;
    QTest::newRow("cellular phone") << quint32(0x000204) << true;
    QTest::newRow("headset") << quint32(0x240404) << false;
    QTest::newRow("laptop") << quint32(0x00010C) << false;
    QTest::newRow("uncategorized") << quint32(0x001F00) << false;
    QTest::newRow("no class") << quint32(0) << false;
}

void TestBluetoothDeviceFinder::onlyPhonesAreListed()
{
    QFETCH(quint32, classOfDevice);
    QFETCH(bool, phone);

    const QBluetoothDeviceInfo info(QBluetoothAddress(QStringLiteral("00:11:22:33:44:55")),
                                    QStringLiteral("Test device"), classOfDevice);

    QCOMPARE(BluetoothDeviceFinder::isPhone(info), phone);
}

QTEST_GUILESS_MAIN(TestBluetoothDeviceFinder)

#include "tst_bluetoothdevicefinder.moc"
