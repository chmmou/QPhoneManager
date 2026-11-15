#include "bluetooth/RfcommTransport.h"
#include "bluetooth/SerialPortProfile.h"

#include "core/protocol/AtSession.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QBuffer>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::protocol;
using qpm::RfcommTransport;
using qpm::test::FakePhone;
using qpm::test::waitFor;

// The tests drive the transport over a stream that stands in for the RFCOMM
// socket: a TCP connection to the fake phone, which speaks the same protocol.
class TestRfcomm : public QObject
{
    Q_OBJECT

public:
    explicit TestRfcomm(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void serialPortProfileIsTheClientsService();
    void atSessionRunsOverTheStream();
    void phoneClosingTheStreamEndsTheTransport();
    void closedStreamCannotBeOpened();
    void writesAfterCloseAreDropped();
    void socketErrorMapsToItsCode_data();
    void socketErrorMapsToItsCode();

private:
    [[nodiscard]] std::unique_ptr<RfcommTransport> openedTransport();

    std::unique_ptr<FakePhone> m_phone;
};

void TestRfcomm::init()
{
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
}

void TestRfcomm::cleanup()
{
    m_phone.reset();
}

std::unique_ptr<RfcommTransport> TestRfcomm::openedTransport()
{
    auto *stream = new QTcpSocket;
    stream->connectToHost(QHostAddress::LocalHost, m_phone->port());
    if (!stream->waitForConnected(5000)) {
        delete stream;
        return nullptr;
    }
    auto transport = std::make_unique<RfcommTransport>(stream);
    QSignalSpy opened(transport.get(), &Transport::opened);
    transport->open();
    if (opened.count() != 1)
        return nullptr;
    return transport;
}

void TestRfcomm::serialPortProfileIsTheClientsService()
{
    QCOMPARE(qpm::serialPortProfileUuid(),
             QBluetoothUuid(QStringLiteral("00001101-0000-1000-8000-00805F9B34FB")));
}

void TestRfcomm::atSessionRunsOverTheStream()
{
    std::unique_ptr<RfcommTransport> transport = openedTransport();
    QVERIFY(transport);
    QVERIFY(transport->isOpen());
    QCOMPARE(transport->peerDescription(), QStringLiteral("Bluetooth"));
    AtSession session(transport.get());

    const QFuture<AtReply> model = session.send(QStringLiteral("ATI"));
    const QFuture<AtReply> battery = session.send(QStringLiteral("AT+CBC"));
    QVERIFY(waitFor(battery));

    QCOMPARE(model.result().status, AtReply::Status::Ok);
    QCOMPARE(model.result().lines, QStringList{m_phone->config().model});
    QCOMPARE(battery.result().lines, QStringList{QStringLiteral("+CBC: 0,72")});
    QVERIFY(m_phone->protocolErrors().isEmpty());
}

void TestRfcomm::phoneClosingTheStreamEndsTheTransport()
{
    std::unique_ptr<RfcommTransport> transport = openedTransport();
    QVERIFY(transport);
    QSignalSpy closed(transport.get(), &Transport::closed);
    QTRY_VERIFY(m_phone->isConnected());

    m_phone->dropConnection();

    QTRY_COMPARE(closed.count(), 1);
    QVERIFY(!transport->isOpen());
}

void TestRfcomm::closedStreamCannotBeOpened()
{
    RfcommTransport transport(new QBuffer);
    QSignalSpy opened(&transport, &Transport::opened);
    QSignalSpy failed(&transport, &Transport::failed);

    transport.open();

    QCOMPARE(opened.count(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().first().value<Error>().code(), ErrorCode::ConnectionClosed);
}

void TestRfcomm::writesAfterCloseAreDropped()
{
    auto *buffer = new QBuffer;
    QVERIFY(buffer->open(QIODevice::ReadWrite));
    RfcommTransport transport(buffer);
    QSignalSpy closed(&transport, &Transport::closed);
    transport.open();
    transport.write("ATI\r\n");
    QCOMPARE(buffer->data(), QByteArrayLiteral("ATI\r\n"));

    transport.close();
    transport.write("AT+CBC\r\n");

    QCOMPARE(closed.count(), 1);
    QVERIFY(!transport.isOpen());
    QCOMPARE(buffer->data(), QByteArrayLiteral("ATI\r\n"));
}

void TestRfcomm::socketErrorMapsToItsCode_data()
{
    QTest::addColumn<QBluetoothSocket::SocketError>("socketError");
    QTest::addColumn<int>("code");

    QTest::newRow("host not found") << QBluetoothSocket::SocketError::HostNotFoundError
                                    << int(ErrorCode::BluetoothServiceNotFound);
    QTest::newRow("service not found") << QBluetoothSocket::SocketError::ServiceNotFoundError
                                       << int(ErrorCode::BluetoothServiceNotFound);
    QTest::newRow("network") << QBluetoothSocket::SocketError::NetworkError
                             << int(ErrorCode::ConnectionClosed);
    QTest::newRow("unknown") << QBluetoothSocket::SocketError::UnknownSocketError
                             << int(ErrorCode::ConnectionClosed);
}

// A connect that BlueZ answers with "Cannot connect to remote profile" ended
// as a closed connection, whose text did not say what to do on the phone.
void TestRfcomm::socketErrorMapsToItsCode()
{
    QFETCH(QBluetoothSocket::SocketError, socketError);
    QFETCH(int, code);

    const Error error =
        RfcommTransport::errorFor(socketError, QStringLiteral("Cannot connect to remote profile"));

    QCOMPARE(error.code(), ErrorCode(code));
    QCOMPARE(error.message(),
             QStringLiteral("Bluetooth connection failed: Cannot connect to remote profile"));
}

QTEST_GUILESS_MAIN(TestRfcomm)

#include "tst_rfcomm.moc"
