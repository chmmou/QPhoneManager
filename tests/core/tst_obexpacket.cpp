#include "core/protocol/ObexPacket.h"

#include <QTest>

using namespace qpm::core;
using namespace qpm::core::protocol;

namespace {

// Byte sequences as the original desktop program sent and received them,
// taken from the protocol reference.
QByteArray hex(const char *text)
{
    return QByteArray::fromHex(text);
}

QByteArray encodeOrFail(const ObexPacket &packet)
{
    const Result<QByteArray> encoded = packet.encode();
    return encoded.hasValue() ? encoded.value() : QByteArray();
}

} // namespace

class TestObexPacket : public QObject
{
    Q_OBJECT

public:
    explicit TestObexPacket(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void connectRequestMatchesTheClientLog();
    void getRequestMatchesTheClientLog();
    void singlePacketPutMatchesTheClientLog();
    void setPathToTheRootMatchesTheClientLog();
    void disconnectMatchesTheClientLog();
    void connectResponseIsDecoded();
    void endOfBodyResponseIsDecoded();
    void createdAnswerCarriesTheLuidInAppParameters();
    void lengthBigIsWrittenWithoutALengthField();
    void unknownHeadersAreSkipped();
    void malformedPacketsAreRejected_data();
    void malformedPacketsAreRejected();
    void oversizedPacketIsRejected();
};

void TestObexPacket::connectRequestMatchesTheClientLog()
{
    ObexPacket packet;
    packet.code = obex::opConnect;
    packet.layout = ObexPacket::Layout::Connect;
    packet.target = QByteArrayLiteral("IRMC-SYNC");

    QCOMPARE(encodeOrFail(packet), hex("8000131000FFFF46000C49524D432D53594E43"));
}

void TestObexPacket::getRequestMatchesTheClientLog()
{
    ObexPacket packet;
    packet.code = obex::opGetFinal;
    packet.connectionId = 1;
    packet.name = QStringLiteral("telecom/pb/info.log");

    QCOMPARE(
        encodeOrFail(packet),
        hex("830033CB0000000101002B00740065006C00650063006F006D002F00700062002F0069006E0066006F"
            "002E006C006F00670000"));
}

void TestObexPacket::singlePacketPutMatchesTheClientLog()
{
    ObexPacket packet;
    packet.code = obex::opPutFinal;
    packet.connectionId = 1;
    packet.name = QStringLiteral("telecom/msg/setflags");
    packet.length = 17;
    packet.bodyKind = ObexPacket::BodyKind::EndOfBody;
    packet.body = QByteArrayLiteral("IncludeSheduleSMS");

    QCOMPARE(encodeOrFail(packet),
             hex("82004ECB0000000101002D00740065006C00650063006F006D002F006D00730067002F0073006500"
                 "740066006C0061006700730000C300000011490014496E636C75646553686564756C65534D53"));
}

void TestObexPacket::setPathToTheRootMatchesTheClientLog()
{
    ObexPacket packet;
    packet.code = obex::opSetPath;
    packet.layout = ObexPacket::Layout::SetPath;
    packet.setPathFlags = 0x02;
    packet.connectionId = 1;
    packet.name = QString();

    QCOMPARE(encodeOrFail(packet), hex("85000D0200CB00000001010003"));
}

void TestObexPacket::disconnectMatchesTheClientLog()
{
    ObexPacket packet;
    packet.code = obex::opDisconnect;
    packet.connectionId = 1;

    QCOMPARE(encodeOrFail(packet), hex("810008CB00000001"));
}

void TestObexPacket::connectResponseIsDecoded()
{
    const Result<ObexPacket> packet = ObexPacket::decode(
        hex("A000181000FFFFCB000000014A000C49524D432D53594E43"), ObexPacket::Layout::Connect);

    QVERIFY(packet.hasValue());
    QCOMPARE(packet.value().code, obex::rspOk);
    QCOMPARE(packet.value().version, quint8(0x10));
    QCOMPARE(packet.value().maxPacketLength, quint16(0xFFFF));
    QCOMPARE(packet.value().connectionId, std::optional<quint32>(1));
    QCOMPARE(packet.value().who, QByteArrayLiteral("IRMC-SYNC"));
}

void TestObexPacket::endOfBodyResponseIsDecoded()
{
    const Result<ObexPacket> packet = ObexPacket::decode(
        hex("A00016490013546F74616C2D5265636F7264733A3137"), ObexPacket::Layout::Plain);

    QVERIFY(packet.hasValue());
    QVERIFY(packet.value().bodyKind == ObexPacket::BodyKind::EndOfBody);
    QCOMPARE(packet.value().body, QByteArrayLiteral("Total-Records:17"));
}

void TestObexPacket::createdAnswerCarriesTheLuidInAppParameters()
{
    // Two spaces, twelve hex digits, one space: 15 bytes of App-Parameters.
    const Result<ObexPacket> packet = ObexPacket::decode(
        hex("A000154C0012202030303030303030303030344420"), ObexPacket::Layout::Plain);

    QVERIFY(packet.hasValue());
    QCOMPARE(packet.value().appParameters.value_or(QByteArray()),
             QByteArrayLiteral("  00000000004D "));
}

void TestObexPacket::lengthBigIsWrittenWithoutALengthField()
{
    ObexPacket packet;
    packet.code = obex::rspOk;
    packet.name = QStringLiteral("a.bin");
    packet.appParameters = QByteArrayLiteral("TCP:5216");
    packet.lengthBig = 0x0000000123456789ULL;

    const QByteArray bytes = encodeOrFail(packet);
    QVERIFY(bytes.contains(hex("330000000123456789")));
    const Result<ObexPacket> decoded = ObexPacket::decode(bytes, ObexPacket::Layout::Plain);
    QVERIFY(decoded.hasValue());
    QCOMPARE(decoded.value().lengthBig, std::optional<quint64>(0x0000000123456789ULL));
    QCOMPARE(decoded.value().name, std::optional<QString>(QStringLiteral("a.bin")));
    QCOMPARE(decoded.value().appParameters.value_or(QByteArray()), QByteArrayLiteral("TCP:5216"));
}

void TestObexPacket::unknownHeadersAreSkipped()
{
    // A one-byte header (0x97) and an unknown byte sequence (0x4F) before the body.
    const Result<ObexPacket> packet =
        ObexPacket::decode(hex("A0000E97014F0005ABCD49000478"), ObexPacket::Layout::Plain);

    QVERIFY(packet.hasValue());
    QCOMPARE(packet.value().body, QByteArrayLiteral("x"));
}

void TestObexPacket::malformedPacketsAreRejected_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<int>("layout");

    QTest::newRow("too short") << hex("A000") << int(ObexPacket::Layout::Plain);
    QTest::newRow("length mismatch") << hex("A00005") << int(ObexPacket::Layout::Plain);
    QTest::newRow("truncated header") << hex("A0000549") << int(ObexPacket::Layout::Plain);
    QTest::newRow("header longer than packet")
        << hex("A00006490009") << int(ObexPacket::Layout::Plain);
    QTest::newRow("header length below three")
        << hex("A00006490001") << int(ObexPacket::Layout::Plain);
    QTest::newRow("odd name length") << hex("A0000701000400") << int(ObexPacket::Layout::Plain);
    QTest::newRow("connect fields missing")
        << hex("A000051000") << int(ObexPacket::Layout::Connect);
    QTest::newRow("truncated connection id") << hex("A00005CB00") << int(ObexPacket::Layout::Plain);
}

void TestObexPacket::malformedPacketsAreRejected()
{
    QFETCH(QByteArray, bytes);
    QFETCH(int, layout);

    const Result<ObexPacket> packet =
        ObexPacket::decode(bytes, static_cast<ObexPacket::Layout>(layout));
    QVERIFY(!packet.hasValue());
    QCOMPARE(packet.error().code(), ErrorCode::ProtocolError);
}

void TestObexPacket::oversizedPacketIsRejected()
{
    ObexPacket packet;
    packet.code = obex::opPutFinal;
    packet.bodyKind = ObexPacket::BodyKind::EndOfBody;
    packet.body = QByteArray(obex::maxPacketSize, 'x');

    const Result<QByteArray> encoded = packet.encode();
    QVERIFY(!encoded.hasValue());
    QCOMPARE(encoded.error().code(), ErrorCode::LimitExceeded);
}

QTEST_GUILESS_MAIN(TestObexPacket)

#include "tst_obexpacket.moc"
