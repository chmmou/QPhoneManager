#include "core/protocol/Discovery.h"

#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTest>
#include <QUdpSocket>

using namespace qpm::core;
using namespace qpm::core::protocol;

namespace {

// The worked example of the protocol reference, computed with dummy data.
constexpr char examplePlain[] = "MyPhoneExplorer_Client|192.168.1.2|ABC|Model X";
constexpr char exampleWire[] =
    "002915373C17162E1D095E5F42533F0F06333A1C1D1F19480B021E077B686B6E7D4B"
    "0F2A273A4E7D5F52283C6507";
constexpr char broadcastPrefixWire[] = "002915373C17162E1D095E5F42533F0F06333A1C1D1F3A3B405F51522E"
                                       "31362B2F";

} // namespace

class TestDiscovery : public QObject
{
    Q_OBJECT

public:
    explicit TestDiscovery(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void encodingMatchesTheDocumentedExample();
    void decodingReadsAddressIdAndModel();
    void announcementFormIsAccepted();
    void foreignDatagramsAreIgnored_data();
    void foreignDatagramsAreIgnored();
    void phoneAnsweringTheProbeIsFound();
};

void TestDiscovery::encodingMatchesTheDocumentedExample()
{
    QCOMPARE(Discovery::encodeDatagram(QString::fromLatin1(examplePlain)), QByteArray(exampleWire));
    QVERIFY(Discovery::encodeDatagram(QStringLiteral("MyPhoneExplorer_Client_Broadcast|x|y|z"))
                .startsWith(broadcastPrefixWire));
}

void TestDiscovery::decodingReadsAddressIdAndModel()
{
    const std::optional<DiscoveredDevice> device = Discovery::decodeDatagram(exampleWire);

    QVERIFY(device.has_value());
    QCOMPARE(device->address, QHostAddress(QStringLiteral("192.168.1.2")));
    QCOMPARE(device->deviceId, QStringLiteral("ABC"));
    QCOMPARE(device->model, QStringLiteral("Model X"));
}

void TestDiscovery::announcementFormIsAccepted()
{
    const QByteArray wire = Discovery::encodeDatagram(
        QStringLiteral("MyPhoneExplorer_Client_Broadcast|10.0.0.5|device-1|Example Phone"));

    const std::optional<DiscoveredDevice> device = Discovery::decodeDatagram(wire);
    QVERIFY(device.has_value());
    QCOMPARE(device->address, QHostAddress(QStringLiteral("10.0.0.5")));
    QCOMPARE(device->deviceId, QStringLiteral("device-1"));
}

void TestDiscovery::foreignDatagramsAreIgnored_data()
{
    QTest::addColumn<QByteArray>("wire");

    QTest::newRow("empty") << QByteArray();
    QTest::newRow("not hex") << QByteArrayLiteral("ZZZZ");
    QTest::newRow("odd length") << QByteArray(exampleWire).chopped(1);
    QTest::newRow("other program")
        << Discovery::encodeDatagram(QStringLiteral("OtherProgram|192.168.1.2|ABC|Model"));
    QTest::newRow("bad address") << Discovery::encodeDatagram(
        QStringLiteral("MyPhoneExplorer_Client|999.1.1.1|ABC|M"));
    QTest::newRow("missing field")
        << Discovery::encodeDatagram(QStringLiteral("MyPhoneExplorer_Client|192.168.1.2|ABC"));
    QTest::newRow("empty id") << Discovery::encodeDatagram(
        QStringLiteral("MyPhoneExplorer_Client|192.168.1.2||M"));
    QTest::newRow("oversized") << QByteArray(2048, 'A');
}

void TestDiscovery::foreignDatagramsAreIgnored()
{
    QFETCH(QByteArray, wire);

    QVERIFY(!Discovery::decodeDatagram(wire).has_value());
}

void TestDiscovery::phoneAnsweringTheProbeIsFound()
{
    QUdpSocket phone;
    QVERIFY(phone.bind(QHostAddress::LocalHost, 0));
    Discovery::Ports ports;
    ports.probeAddress = QHostAddress::LocalHost;
    ports.probePort = phone.localPort();
    ports.receivePort = 0;
    Discovery discovery(ports, nullptr);
    QSignalSpy found(&discovery, &Discovery::deviceFound);
    QSignalSpy probed(&phone, &QUdpSocket::readyRead);

    QVERIFY(discovery.start().hasValue());
    QVERIFY(discovery.receivePort() != 0);
    QVERIFY(probed.count() > 0 || probed.wait());

    // Like the client: answer to the sender's address on the receive port.
    const QNetworkDatagram probe = phone.receiveDatagram();
    phone.writeDatagram(
        Discovery::encodeDatagram(QStringLiteral("MyPhoneExplorer_Client|127.0.0.1|id-7|Phone")),
        probe.senderAddress(), discovery.receivePort());

    QVERIFY(found.wait());
    const auto device = found.first().first().value<DiscoveredDevice>();
    QCOMPARE(device.deviceId, QStringLiteral("id-7"));
    QCOMPARE(device.model, QStringLiteral("Phone"));
    QCOMPARE(discovery.devices().size(), 1);
}

QTEST_GUILESS_MAIN(TestDiscovery)

#include "tst_discovery.moc"
