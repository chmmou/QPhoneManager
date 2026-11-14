#include "core/protocol/PhoneConnection.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QCryptographicHash>
#include <QFile>
#include <QPromise>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::protocol;
using qpm::test::FakePhone;
using qpm::test::waitFor;

namespace {

using State = PhoneConnection::State;
constexpr int connectTimeoutMs = 10000;

QStringList handshakeCommands()
{
    return {QStringLiteral("AT+CGSN"),
            QStringLiteral("AT*PIN?"),
            QStringLiteral("ATI"),
            QStringLiteral("AT+CGMI"),
            QStringLiteral("AT*APPVERSION=2.3.0"),
            QStringLiteral("AT*ECAM=1"),
            QStringLiteral("AT+CBC"),
            QStringLiteral("AT+CSQ"),
            QStringLiteral("AT*SOPS"),
            QStringLiteral("AT*SIM"),
            QStringLiteral("AT+CMGS=?"),
            QStringLiteral("AT*ANDROIDVERSION"),
            QStringLiteral("AT*STATFS")};
}

QString expectedPinCommand(const QString &pin)
{
    return QStringLiteral("AT*PIN=") +
           QString::fromLatin1(
               QCryptographicHash::hash("hash" + pin.toUtf8(), QCryptographicHash::Md5)
                   .toHex()
                   .toUpper());
}

} // namespace

class TestPhoneConnection : public QObject
{
    Q_OBJECT

public:
    explicit TestPhoneConnection(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void handshakeIdentifiesThePhone();
    void handshakeMovesThroughTheStates();
    void correctPinUnlocksThePhone();
    void wrongPinReportsTheRemainingTries();
    void tenWrongPinsLockTheClient();
    void aliveLineUpdatesTheBattery();
    void unsolicitedLinesArePassedOn();
    void phoneBusyWithAnotherComputerIsReported();
    void newerConnectionReplacesTheOlderOne();
    void lostConnectionIsRestoredWhenReconnectIsOn();
    void refusedConnectionIsReported();
    void statusIsPolledAtTheConfiguredInterval();
    void obexSessionsRunOneAfterAnotherAndRefreshTheStatus();
    void notificationsAreFetchedAgainAfterObex();
    void obexWorkFailsWhenTheConnectionDrops();
    void phoneCanConnectToThisComputer();
    void usbConnectionGoesThroughForwardedPorts();
    void runningObexWorkFailsWhenTheConnectionEnds();
    void runningObexWorkFailsWhenTheConnectionIsDestroyed();
    void unansweredStatusPollCountsAsLostLink();
    void statusPollsDoNotPileUpBehindSlowAnswers();
    void droppedLinkBeforeAnyPinIsNoWrongPin();
    void pinIsSentOnceWhileItsAnswerIsPending();

private:
    [[nodiscard]] bool reachReady(PhoneConnection &connection);

    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
};

void TestPhoneConnection::init()
{
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_connection = std::make_unique<PhoneConnection>();
}

void TestPhoneConnection::cleanup()
{
    QVERIFY2(m_phone->protocolErrors().isEmpty(), qPrintable(m_phone->protocolErrors().join(u';')));
    m_connection.reset();
    m_phone.reset();
}

bool TestPhoneConnection::reachReady(PhoneConnection &connection)
{
    connection.connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    return QTest::qWaitFor([&connection]() { return connection.state() == State::Ready; },
                           connectTimeoutMs);
}

void TestPhoneConnection::handshakeIdentifiesThePhone()
{
    QSignalSpy identified(m_connection.get(), &PhoneConnection::deviceIdentified);

    QVERIFY(reachReady(*m_connection));

    QCOMPARE(identified.count(), 1);
    const DeviceIdentity identity = m_connection->identity();
    QCOMPARE(identity.deviceId, m_phone->config().deviceId);
    QCOMPARE(identity.model, QStringLiteral("Example Phone X1"));
    QCOMPARE(identity.manufacturer, QStringLiteral("Example"));
    QCOMPARE(identity.androidApi, 34);
    QCOMPARE(identity.clientVersion, QStringLiteral("4.15"));
    QCOMPARE(identity.androidVersion, QStringLiteral("Android 14"));
    QCOMPARE(identity.androidCodename, QStringLiteral("Upside Down Cake"));
    QCOMPARE(identity.smsCapabilities, QStringLiteral("TXTv2,PING"));
    QCOMPARE(m_connection->status().batteryPercent, std::optional<int>(72));
    QCOMPARE(m_connection->status().charging, std::optional<bool>(false));
    QCOMPARE(m_connection->status().signal, std::optional<int>(17));
    QCOMPARE(m_phone->receivedCommands(), handshakeCommands());
}

void TestPhoneConnection::handshakeMovesThroughTheStates()
{
    QSignalSpy states(m_connection.get(), &PhoneConnection::stateChanged);

    QVERIFY(reachReady(*m_connection));

    QList<State> seen;
    for (const QList<QVariant> &arguments : std::as_const(states))
        seen.append(arguments.first().value<State>());
    QCOMPARE(seen, (QList<State>{State::Connecting, State::Authenticating, State::Handshaking,
                                 State::Ready}));
}

void TestPhoneConnection::correctPinUnlocksThePhone()
{
    m_phone->config().pin = QStringLiteral("2580");
    QSignalSpy pinRequired(m_connection.get(), &PhoneConnection::pinRequired);

    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QVERIFY(pinRequired.wait());
    QCOMPARE(pinRequired.first().first().toInt(), 10);
    QCOMPARE(m_connection->state(), State::Authenticating);
    m_connection->submitPin(QStringLiteral("2580"));

    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), State::Ready, connectTimeoutMs);
    QVERIFY(m_phone->receivedCommands().contains(expectedPinCommand(QStringLiteral("2580"))));
}

void TestPhoneConnection::wrongPinReportsTheRemainingTries()
{
    m_phone->config().pin = QStringLiteral("2580");
    QSignalSpy pinRequired(m_connection.get(), &PhoneConnection::pinRequired);
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QVERIFY(pinRequired.wait());

    m_connection->submitPin(QStringLiteral("0000"));

    QVERIFY(pinRequired.wait());
    QCOMPARE(pinRequired.last().first().toInt(), 9);
    QCOMPARE(m_connection->state(), State::Authenticating);
}

void TestPhoneConnection::tenWrongPinsLockTheClient()
{
    m_phone->config().pin = QStringLiteral("2580");
    QSignalSpy pinRequired(m_connection.get(), &PhoneConnection::pinRequired);
    QSignalSpy failed(m_connection.get(), &PhoneConnection::failed);
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QVERIFY(pinRequired.wait());

    for (int attempt = 1; attempt <= 10; ++attempt) {
        m_connection->submitPin(QStringLiteral("0000"));
        QVERIFY(pinRequired.wait());
        QCOMPARE(pinRequired.last().first().toInt(), 10 - attempt);
    }
    m_connection->submitPin(QStringLiteral("2580"));

    QVERIFY(failed.wait());
    QCOMPARE(failed.first().first().value<Error>().code(), ErrorCode::WrongPin);
    QCOMPARE(m_connection->state(), State::Lost);
}

void TestPhoneConnection::aliveLineUpdatesTheBattery()
{
    QVERIFY(reachReady(*m_connection));
    QSignalSpy status(m_connection.get(), &PhoneConnection::statusUpdated);

    m_phone->sendUnsolicited("*ALIVE: 1,55,0");

    QVERIFY(status.wait());
    const auto updated = status.first().first().value<DeviceStatus>();
    QCOMPARE(updated.batteryPercent, std::optional<int>(55));
    QCOMPARE(updated.charging, std::optional<bool>(true));
}

void TestPhoneConnection::unsolicitedLinesArePassedOn()
{
    QVERIFY(reachReady(*m_connection));
    QSignalSpy unsolicited(m_connection.get(), &PhoneConnection::unsolicited);

    m_phone->sendUnsolicited("+CMTI: \"ME\",12");

    QVERIFY(unsolicited.wait());
    QCOMPARE(unsolicited.first().first().toString(), QStringLiteral("+CMTI: \"ME\",12"));
}

void TestPhoneConnection::phoneBusyWithAnotherComputerIsReported()
{
    m_phone->config().rejectAsAlreadyConnected = true;
    QSignalSpy failed(m_connection.get(), &PhoneConnection::failed);

    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());

    QVERIFY(failed.wait());
    QCOMPARE(failed.first().first().value<Error>().code(), ErrorCode::AlreadyConnected);
    QCOMPARE(m_connection->state(), State::Lost);
}

void TestPhoneConnection::newerConnectionReplacesTheOlderOne()
{
    QVERIFY(reachReady(*m_connection));
    QSignalSpy failed(m_connection.get(), &PhoneConnection::failed);
    PhoneConnection newer;

    QVERIFY(reachReady(newer));

    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), State::Lost, connectTimeoutMs);
    QCOMPARE(failed.first().first().value<Error>().code(), ErrorCode::ConnectionClosed);
    QCOMPARE(newer.state(), State::Ready);
}

void TestPhoneConnection::lostConnectionIsRestoredWhenReconnectIsOn()
{
    PhoneConnection::Options options;
    options.reconnectDelay = std::chrono::milliseconds(100);
    m_connection->setOptions(options);
    QVERIFY(reachReady(*m_connection));
    QSignalSpy failed(m_connection.get(), &PhoneConnection::failed);
    QSignalSpy states(m_connection.get(), &PhoneConnection::stateChanged);

    m_phone->dropConnection();

    QVERIFY(failed.wait());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), State::Ready, connectTimeoutMs);
    QCOMPARE(states.first().first().value<State>(), State::Lost);
    QCOMPARE(failed.count(), 1);
}

void TestPhoneConnection::refusedConnectionIsReported()
{
    quint16 closedPort = 0;
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        closedPort = server.serverPort();
    }
    QSignalSpy failed(m_connection.get(), &PhoneConnection::failed);

    m_connection->connectTcp(QStringLiteral("127.0.0.1"), closedPort);

    QVERIFY(failed.wait());
    QCOMPARE(failed.first().first().value<Error>().code(), ErrorCode::ConnectionClosed);
    QCOMPARE(m_connection->state(), State::Lost);
}

void TestPhoneConnection::statusIsPolledAtTheConfiguredInterval()
{
    PhoneConnection::Options options;
    options.statusInterval = std::chrono::milliseconds(50);
    m_connection->setOptions(options);
    QVERIFY(reachReady(*m_connection));

    QTRY_VERIFY_WITH_TIMEOUT(m_connection->status().cpuPercent.has_value(), connectTimeoutMs);

    QCOMPARE(m_connection->status().cpuPercent, std::optional<int>(12));
    QCOMPARE(m_connection->status().temperatureCelsius, std::optional<int>(24));
    QCOMPARE(m_connection->status().voltageMillivolts, std::optional<int>(3915));
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("AT+CBCEX")));
}

void TestPhoneConnection::obexSessionsRunOneAfterAnotherAndRefreshTheStatus()
{
    QVERIFY(reachReady(*m_connection));
    m_phone->clearLog();
    QSignalSpy states(m_connection.get(), &PhoneConnection::stateChanged);
    const std::function<QFuture<Result<QByteArray>>(ObexClient &)> readInfo = [](ObexClient &obex) {
        return obex.get(QStringLiteral("telecom/pb/info.log"))
            .then([](const Result<ObexResponse> &response) -> Result<QByteArray> {
                if (!response.hasValue())
                    return response.error();
                return response.value().body;
            });
    };

    const QFuture<Result<QByteArray>> first =
        m_connection->withObex<QByteArray>(ObexClient::Target::IrmcSync, readInfo);
    const QFuture<Result<QByteArray>> second =
        m_connection->withObex<QByteArray>(ObexClient::Target::IrmcSync, readInfo);
    QVERIFY(waitFor(second));

    QCOMPARE(first.result().value(), QByteArrayLiteral("Total-Records:0"));
    QCOMPARE(second.result().value(), QByteArrayLiteral("Total-Records:0"));
    QTRY_COMPARE_WITH_TIMEOUT(m_phone->receivedCommands().size(), 12, connectTimeoutMs);
    const QStringList session = {QStringLiteral("AT+CPROT=0"),
                                 QStringLiteral("OBEX CONNECT IRMC-SYNC"),
                                 QStringLiteral("OBEX GET telecom/pb/info.log"),
                                 QStringLiteral("OBEX DISCONNECT"),
                                 QStringLiteral("AT+CBC"),
                                 QStringLiteral("AT+CSQ")};
    QCOMPARE(m_phone->receivedCommands(), session + session);
    QCOMPARE(states.first().first().value<State>(), State::Busy);
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), State::Ready, connectTimeoutMs);
}

void TestPhoneConnection::notificationsAreFetchedAgainAfterObex()
{
    PhoneConnection::Options options;
    options.notifications = true;
    m_connection->setOptions(options);
    QVERIFY(reachReady(*m_connection));
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("AT*NOTIFY=2")));
    m_phone->clearLog();

    const QFuture<Result<void>> session =
        m_connection->withObex<void>(ObexClient::Target::IrmcSync, [](ObexClient &) {
            return QtFuture::makeReadyValueFuture(Result<void>());
        });
    QVERIFY(waitFor(session));

    QTRY_VERIFY_WITH_TIMEOUT(m_phone->receivedCommands().contains(QStringLiteral("AT*NOTIR")),
                             connectTimeoutMs);
}

void TestPhoneConnection::obexWorkFailsWhenTheConnectionDrops()
{
    QVERIFY(reachReady(*m_connection));
    FakePhone *phone = m_phone.get();

    const QFuture<Result<QByteArray>> work =
        m_connection->withObex<QByteArray>(ObexClient::Target::IrmcSync, [phone](ObexClient &obex) {
            phone->dropConnection();
            return obex.get(QStringLiteral("telecom/pb/info.log"))
                .then([](const Result<ObexResponse> &response) -> Result<QByteArray> {
                    if (!response.hasValue())
                        return response.error();
                    return response.value().body;
                });
        });
    QVERIFY(waitFor(work));

    QCOMPARE(work.result().error().code(), ErrorCode::ConnectionClosed);
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), State::Lost, connectTimeoutMs);
}

void TestPhoneConnection::phoneCanConnectToThisComputer()
{
    QVERIFY(m_connection->listenForPhone(0, QHostAddress::LocalHost).hasValue());
    QCOMPARE(m_connection->state(), State::Discovering);

    m_phone->dialOut(QHostAddress::LocalHost, m_connection->listeningPort());

    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), State::Ready, connectTimeoutMs);
    QCOMPARE(m_connection->identity().deviceId, m_phone->config().deviceId);
    m_connection->stopListening();
    QVERIFY(!m_connection->isListening());
}

void TestPhoneConnection::usbConnectionGoesThroughForwardedPorts()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString calls = directory.filePath(QStringLiteral("calls"));
    const QString adb = directory.filePath(QStringLiteral("adb"));
    QFile script(adb);
    QVERIFY(script.open(QIODevice::WriteOnly));
    script.write("#!/bin/sh\n"
                 "echo \"$@\" >> '" +
                 calls.toUtf8() +
                 "'\n"
                 "case \"$3\" in\n"
                 "forward)\n"
                 "  case \"$5\" in\n"
                 "  tcp:5210) echo " +
                 QByteArray::number(m_phone->port()) +
                 " ;;\n"
                 "  tcp:5213) echo 40013 ;;\n"
                 "  tcp:5216) echo 40016 ;;\n"
                 "  tcp:5217) echo 40017 ;;\n"
                 "  esac ;;\n"
                 "shell) echo 'Starting: Intent' ;;\n"
                 "esac\n"
                 "exit 0\n");
    script.close();
    QVERIFY(script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

    m_connection->connectAdb(adb, QStringLiteral("emulator-5554"));

    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), State::Ready, connectTimeoutMs);
    const std::optional<Endpoint> fileChannel = m_connection->sideChannelEndpoint(5216);
    QVERIFY(fileChannel.has_value());
    QCOMPARE(fileChannel->host, QStringLiteral("127.0.0.1"));
    QCOMPARE(fileChannel->port, quint16(40016));
    QFile log(calls);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QByteArray invoked = log.readAll();
    QVERIFY(invoked.startsWith("start-server\n"));
    QVERIFY(invoked.contains("-s emulator-5554 shell am start -n "
                             "com.fjsoft.myphoneexplorer.client/.MainActivity --ez adb true\n"));
}

// OBEX work that still ran when the connection ended kept its future open for
// good, although its OBEX client was already handed over for deletion.
void TestPhoneConnection::runningObexWorkFailsWhenTheConnectionEnds()
{
    QVERIFY(reachReady(*m_connection));
    QPromise<Result<void>> endless;
    endless.start();
    bool started = false;

    const QFuture<Result<void>> work = m_connection->withObex<void>(
        ObexClient::Target::IrmcSync, [&endless, &started](ObexClient &) {
            started = true;
            return endless.future();
        });
    QTRY_VERIFY(started);
    m_connection->disconnectFromPhone();

    QVERIFY(waitFor(work, 2000));
    QCOMPARE(work.result().error().code(), ErrorCode::ConnectionClosed);
    endless.addResult(Result<void>());
    endless.finish();
}

// Destroying the connection while OBEX work ran left that work's future
// without a result. Later the cancelled continuations of that work read
// members of the half-destroyed connection; only a build with
// UndefinedBehaviorSanitizer reports that read.
void TestPhoneConnection::runningObexWorkFailsWhenTheConnectionIsDestroyed()
{
    QVERIFY(reachReady(*m_connection));
    QPromise<Result<void>> endless;
    endless.start();
    bool started = false;

    const QFuture<Result<void>> work = m_connection->withObex<void>(
        ObexClient::Target::IrmcSync, [&endless, &started](ObexClient &) {
            started = true;
            return endless.future();
        });
    QTRY_VERIFY(started);
    m_connection.reset();

    QVERIFY(work.isFinished());
    QVERIFY(!work.isCanceled());
    QCOMPARE(work.result().error().code(), ErrorCode::ConnectionClosed);
    endless.finish();
}

// A status poll that got no answer was dropped without a word, so a dead link
// stayed Ready and was never reconnected.
void TestPhoneConnection::unansweredStatusPollCountsAsLostLink()
{
    PhoneConnection::Options options;
    options.statusInterval = std::chrono::milliseconds(50);
    options.commandTimeout = std::chrono::milliseconds(300);
    options.reconnectDelay = std::chrono::milliseconds(50);
    m_connection->setOptions(options);
    QVERIFY(reachReady(*m_connection));
    QSignalSpy failed(m_connection.get(), &PhoneConnection::failed);
    QSignalSpy states(m_connection.get(), &PhoneConnection::stateChanged);
    m_phone->config().unanswered.insert(QStringLiteral("AT+CBCEX"));

    QVERIFY(failed.wait());
    QCOMPARE(failed.first().first().value<Error>().code(), ErrorCode::Timeout);
    QCOMPARE(states.first().first().value<State>(), State::Lost);
    m_phone->config().unanswered.clear();
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), State::Ready, connectTimeoutMs);
}

// Each tick of the status timer queued a new round of three commands, also
// while the previous round still waited for its answers. On a slow link the
// rounds piled up, and every other command waited behind all of them.
void TestPhoneConnection::statusPollsDoNotPileUpBehindSlowAnswers()
{
    PhoneConnection::Options options;
    options.statusInterval = std::chrono::milliseconds(10);
    m_connection->setOptions(options);
    QVERIFY(reachReady(*m_connection));
    QSignalSpy status(m_connection.get(), &PhoneConnection::statusUpdated);
    m_phone->config().replyDelay = std::chrono::milliseconds(100);

    QTRY_VERIFY_WITH_TIMEOUT(status.count() >= 3, connectTimeoutMs);
    const QFuture<AtReply> maker = m_connection->session()->send(QStringLiteral("AT+CGMI"));

    QVERIFY(waitFor(maker, 3000));
    QCOMPARE(maker.result().status, AtReply::Status::Ok);
}

// A dropped link while the PIN was asked for counted as a client locked by
// wrong PINs, although no PIN had been sent at all.
void TestPhoneConnection::droppedLinkBeforeAnyPinIsNoWrongPin()
{
    m_phone->config().pin = QStringLiteral("2580");
    QSignalSpy pinRequired(m_connection.get(), &PhoneConnection::pinRequired);
    QSignalSpy failed(m_connection.get(), &PhoneConnection::failed);
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QVERIFY(pinRequired.wait());

    m_phone->dropConnection();

    QVERIFY(failed.wait());
    QCOMPARE(failed.first().first().value<Error>().code(), ErrorCode::ConnectionClosed);
    QCOMPARE(m_connection->state(), State::Lost);
}

// A second submitPin() while the first answer was outstanding sent the PIN
// again, and two "ACCESS OK" answers ran the handshake twice.
void TestPhoneConnection::pinIsSentOnceWhileItsAnswerIsPending()
{
    m_phone->config().pin = QStringLiteral("2580");
    QSignalSpy pinRequired(m_connection.get(), &PhoneConnection::pinRequired);
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QVERIFY(pinRequired.wait());

    m_connection->submitPin(QStringLiteral("2580"));
    m_connection->submitPin(QStringLiteral("2580"));

    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), State::Ready, connectTimeoutMs);
    QCOMPARE(m_phone->receivedCommands().count(expectedPinCommand(QStringLiteral("2580"))), 1);
}

QTEST_GUILESS_MAIN(TestPhoneConnection)

#include "tst_phoneconnection.moc"
