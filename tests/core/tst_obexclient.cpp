#include "core/codec/SafeXml.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/ObexClient.h"
#include "core/protocol/TcpTransport.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::protocol;
using qpm::test::FakePhone;
using qpm::test::FakeSyncObject;
using qpm::test::waitFor;

namespace {

QByteArray note(quint32 luid, const QByteArray &body)
{
    return "BEGIN:VNOTE\r\nBODY:" + body + "\r\nX-IRMC-LUID:" +
           QStringLiteral("%1").arg(luid, 12, 16, QLatin1Char('0')).toUpper().toLatin1() +
           "\r\nEND:VNOTE\r\n";
}

// Notes that together need several OBEX packets.
QList<FakeSyncObject> manyNotes(int count)
{
    QList<FakeSyncObject> notes;
    for (int i = 1; i <= count; ++i)
        notes.append({quint32(i), note(quint32(i), QByteArray(1000, char('a' + i % 26)))});
    return notes;
}

QByteArray dumpOf(const QList<FakeSyncObject> &objects)
{
    QByteArray dump;
    for (const FakeSyncObject &object : objects)
        dump += object.data + "\r\n";
    return dump;
}

} // namespace

class TestObexClient : public QObject
{
    Q_OBJECT

public:
    explicit TestObexClient(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void connectAndDisconnectReturnToAtMode();
    void bodyOverSeveralPacketsIsAssembled();
    void keepAlivesAreAnsweredWithExactlyThreeBytes();
    void syncStartProgressIsReportedButNotKept();
    void createAnswerYieldsTheLuid();
    void errorResponsesMapToErrorCodes_data();
    void errorResponsesMapToErrorCodes();
    void packetAboveTheLimitIsNotSent();
    void abortStopsARunningTransfer();
    void cancellingTheFutureAbortsTheTransfer();
    void fileBrowserNavigatesListsReadsAndWrites();
    void socketUploadGoesOverTheSideChannel();
    void socketDownloadComesOverTheSideChannel();
    void lostConnectionFailsOpenRequests();
    void timeoutEndsTheObexSession();
    void connectionChangeRightAfterDisconnectEndsTheSessionCleanly();
    void phoneExceptionKeepsOnlyItsClassName_data();
    void phoneExceptionKeepsOnlyItsClassName();
    void refusedConnectReportsItsMappedError_data();
    void refusedConnectReportsItsMappedError();

private:
    [[nodiscard]] bool connectTo(ObexClient::Target target);

    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<TcpTransport> m_transport;
    std::unique_ptr<AtSession> m_session;
    std::unique_ptr<ObexClient> m_obex;
};

void TestObexClient::init()
{
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_transport = std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), m_phone->port());
    m_session = std::make_unique<AtSession>(m_transport.get());
    m_obex = std::make_unique<ObexClient>(m_session.get());
    QSignalSpy opened(m_transport.get(), &Transport::opened);
    m_transport->open();
    QVERIFY(opened.wait());
}

void TestObexClient::cleanup()
{
    QVERIFY2(m_phone->protocolErrors().isEmpty(), qPrintable(m_phone->protocolErrors().join(u';')));
    m_obex.reset();
    m_session.reset();
    m_transport.reset();
    m_phone.reset();
}

bool TestObexClient::connectTo(ObexClient::Target target)
{
    const QFuture<Result<void>> connected = m_obex->connectTarget(target);
    return waitFor(connected) && connected.result().hasValue();
}

void TestObexClient::connectAndDisconnectReturnToAtMode()
{
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));
    QVERIFY(m_session->isInObexMode());
    QVERIFY(m_obex->isConnected());

    const QFuture<Result<void>> disconnected = m_obex->disconnectTarget();
    QVERIFY(waitFor(disconnected));
    QVERIFY(disconnected.result().hasValue());
    QVERIFY(!m_session->isInObexMode());

    const QFuture<AtReply> battery = m_session->send(QStringLiteral("AT+CBC"));
    QVERIFY(waitFor(battery));
    QCOMPARE(battery.result().status, AtReply::Status::Ok);
    QCOMPARE(m_phone->receivedCommands(),
             (QStringList{QStringLiteral("AT+CPROT=0"), QStringLiteral("OBEX CONNECT IRMC-SYNC"),
                          QStringLiteral("OBEX DISCONNECT"), QStringLiteral("AT+CBC")}));
}

void TestObexClient::bodyOverSeveralPacketsIsAssembled()
{
    const QList<FakeSyncObject> notes = manyNotes(200);
    m_phone->setObjects(QStringLiteral("notes"), notes);
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));
    QSignalSpy progress(m_obex.get(), &ObexClient::progress);

    const QFuture<Result<ObexResponse>> dump = m_obex->get(QStringLiteral("telecom/notes.vnt"));
    QVERIFY(waitFor(dump));

    QVERIFY(dump.result().hasValue());
    QCOMPARE(dump.result().value().body, dumpOf(notes));
    QVERIFY(progress.count() >= 3);
}

void TestObexClient::keepAlivesAreAnsweredWithExactlyThreeBytes()
{
    m_phone->setObjects(QStringLiteral("notes"), manyNotes(100));
    m_phone->config().paddingBeforeReply = 3;
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));

    const QFuture<Result<ObexResponse>> info =
        m_obex->get(QStringLiteral("telecom/notes/info.log"));
    const QFuture<Result<ObexResponse>> dump = m_obex->get(QStringLiteral("telecom/notes.vnt"));
    const QFuture<Result<ObexResponse>> counter =
        m_obex->get(QStringLiteral("telecom/notes/luid/cc.log"));
    QVERIFY(waitFor(counter));

    QCOMPARE(info.result().value().body, QByteArrayLiteral("Total-Records:100"));
    QCOMPARE(dump.result().value().body, dumpOf(manyNotes(100)));
    QCOMPARE(counter.result().value().body, QByteArrayLiteral("0"));
}

void TestObexClient::syncStartProgressIsReportedButNotKept()
{
    m_phone->config().initsyncProgress = 2;
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));
    QSignalSpy progress(m_obex.get(), &ObexClient::progress);

    const QFuture<Result<ObexResponse>> initsync =
        m_obex->get(QStringLiteral("telecom/pb/initsync"));
    QVERIFY(waitFor(initsync));

    QVERIFY(initsync.result().hasValue());
    QVERIFY(initsync.result().value().body.isEmpty());
    QCOMPARE(progress.count(), 2);
    QCOMPARE(progress.at(1).at(0).toULongLong(), 2ULL);
    QCOMPARE(progress.at(1).at(1).value<std::optional<quint64>>(), std::optional<quint64>(2));
}

void TestObexClient::createAnswerYieldsTheLuid()
{
    m_phone->setObjects(QStringLiteral("notes"), manyNotes(4));
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));

    const QFuture<Result<ObexResponse>> created = m_obex->put(
        QStringLiteral("telecom/notes/luid/.vnt"), "BEGIN:VNOTE\r\nBODY:New\r\nEND:VNOTE\r\n");
    QVERIFY(waitFor(created));

    QVERIFY(created.result().hasValue());
    QCOMPARE(ObexClient::createdLuid(created.result().value().appParams),
             std::optional<quint32>(5));
    QCOMPARE(m_phone->changeCounter(QStringLiteral("notes")), 1ULL);
}

void TestObexClient::errorResponsesMapToErrorCodes_data()
{
    QTest::addColumn<int>("response");
    QTest::addColumn<int>("code");

    QTest::newRow("unauthorized") << 0xC1 << int(ErrorCode::PermissionDenied);
    QTest::newRow("forbidden") << 0xC3 << int(ErrorCode::AccessDenied);
    QTest::newRow("not found") << 0xC4 << int(ErrorCode::NotFound);
    QTest::newRow("database locked") << 0xE1 << int(ErrorCode::NotFound);
    QTest::newRow("request timeout") << 0xC8 << int(ErrorCode::Timeout);
    QTest::newRow("conflict") << 0xC9 << int(ErrorCode::Rejected);
    QTest::newRow("precondition") << 0xCC << int(ErrorCode::ProtocolError);
    QTest::newRow("internal error") << 0xD0 << int(ErrorCode::ProtocolError);
    QTest::newRow("not implemented") << 0xD1 << int(ErrorCode::NotSupported);
    QTest::newRow("storage full") << 0xE0 << int(ErrorCode::IoFailure);
}

void TestObexClient::errorResponsesMapToErrorCodes()
{
    QFETCH(int, response);
    QFETCH(int, code);
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));
    m_phone->setFailNext(quint8(response));

    const QFuture<Result<ObexResponse>> answer = m_obex->get(QStringLiteral("telecom/pb/info.log"));
    QVERIFY(waitFor(answer));

    QVERIFY(!answer.result().hasValue());
    QCOMPARE(int(answer.result().error().code()), code);
}

void TestObexClient::packetAboveTheLimitIsNotSent()
{
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));

    const QFuture<Result<ObexResponse>> put =
        m_obex->put(QStringLiteral("telecom/notes/luid/.vnt"), QByteArray(70000, 'x'));
    QVERIFY(waitFor(put));

    QCOMPARE(put.result().error().code(), ErrorCode::LimitExceeded);
    QVERIFY(
        !m_phone->receivedCommands().contains(QStringLiteral("OBEX PUT telecom/notes/luid/.vnt")));
}

void TestObexClient::abortStopsARunningTransfer()
{
    m_phone->setObjects(QStringLiteral("notes"), manyNotes(1000));
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));
    QSignalSpy progress(m_obex.get(), &ObexClient::progress);

    const QFuture<Result<ObexResponse>> dump = m_obex->get(QStringLiteral("telecom/notes.vnt"));
    QVERIFY(progress.wait());
    const QFuture<Result<void>> aborted = m_obex->abort();
    QVERIFY(waitFor(dump));
    QVERIFY(waitFor(aborted));

    QCOMPARE(dump.result().error().code(), ErrorCode::Cancelled);
    QVERIFY(aborted.result().hasValue());
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("OBEX ABORT")));
    const QFuture<Result<ObexResponse>> info =
        m_obex->get(QStringLiteral("telecom/notes/info.log"));
    QVERIFY(waitFor(info));
    QCOMPARE(info.result().value().body, QByteArrayLiteral("Total-Records:1000"));
}

void TestObexClient::cancellingTheFutureAbortsTheTransfer()
{
    m_phone->setObjects(QStringLiteral("notes"), manyNotes(1000));
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));
    QSignalSpy progress(m_obex.get(), &ObexClient::progress);

    QFuture<Result<ObexResponse>> dump = m_obex->get(QStringLiteral("telecom/notes.vnt"));
    QVERIFY(progress.wait());
    dump.cancel();

    const QFuture<Result<ObexResponse>> info =
        m_obex->get(QStringLiteral("telecom/notes/info.log"));
    QVERIFY(waitFor(info));
    QCOMPARE(info.result().value().body, QByteArrayLiteral("Total-Records:1000"));
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("OBEX ABORT")));
}

void TestObexClient::fileBrowserNavigatesListsReadsAndWrites()
{
    const QDateTime modified(QDate(2026, 5, 1), QTime(10, 0), QTimeZone::UTC);
    m_phone->addFile(QStringLiteral("\\sdcard\\DCIM\\a.jpg"), "jpeg data", modified);
    QVERIFY(connectTo(ObexClient::Target::FileBrowser));

    auto succeeds = [](const QFuture<Result<void>> &future) {
        return waitFor(future) && future.result().hasValue();
    };
    QVERIFY(succeeds(m_obex->setPath(QString(), ObexClient::SetPathFlag::DontCreate)));
    QVERIFY(
        succeeds(m_obex->setPath(QStringLiteral("sdcard"), ObexClient::SetPathFlag::DontCreate)));

    const QFuture<Result<ObexResponse>> listing = m_obex->get(
        QString(), QByteArrayLiteral("x-obex/folder-listing"), std::chrono::seconds(30));
    QVERIFY(waitFor(listing));
    const Result<codec::XmlElement> tree = codec::parseSafeXml(listing.result().value().body);
    QVERIFY(tree.hasValue());
    QCOMPARE(tree.value().children.at(1).attribute(QStringLiteral("name")), QStringLiteral("DCIM"));

    QVERIFY(succeeds(m_obex->setPath(QStringLiteral("DCIM"), ObexClient::SetPathFlag::DontCreate)));
    const QFuture<Result<ObexResponse>> photo = m_obex->get(QStringLiteral("a.jpg"));
    QVERIFY(waitFor(photo));
    QCOMPARE(photo.result().value().body, QByteArrayLiteral("jpeg data"));

    ObexClient::PutOptions options;
    options.modified = modified;
    const QFuture<Result<ObexResponse>> written =
        m_obex->put(QStringLiteral("b.txt"), "text", options);
    QVERIFY(waitFor(written));
    QVERIFY(written.result().hasValue());
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\DCIM\\b.txt")).value().modified, modified);

    QVERIFY(succeeds(m_obex->move(QStringLiteral("b.txt"), QStringLiteral("c.txt"))));
    QVERIFY(m_phone->file(QStringLiteral("\\sdcard\\DCIM\\c.txt")).has_value());
    const QFuture<Result<ObexResponse>> removed = m_obex->remove(QStringLiteral("c.txt"));
    QVERIFY(waitFor(removed));
    QVERIFY(removed.result().hasValue());
    QVERIFY(!m_phone->file(QStringLiteral("\\sdcard\\DCIM\\c.txt")).has_value());

    const QFuture<Result<void>> missing =
        m_obex->setPath(QStringLiteral("missing"), ObexClient::SetPathFlag::DontCreate);
    QVERIFY(waitFor(missing));
    QCOMPARE(missing.result().error().code(), ErrorCode::NotFound);
    QVERIFY(succeeds(m_obex->setPath(QStringLiteral("New folder"), ObexClient::SetPathFlag::None)));
    QVERIFY(succeeds(m_obex->setPath(QString(), ObexClient::SetPathFlag::Backup)));
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("OBEX SETPATH New folder")));
}

void TestObexClient::socketUploadGoesOverTheSideChannel()
{
    m_phone->addFolder(QStringLiteral("\\sdcard"));
    QVERIFY(connectTo(ObexClient::Target::FileBrowser));
    const QByteArray data(100000, 'u');
    const QDateTime modified(QDate(2026, 5, 2), QTime(8, 30), QTimeZone::UTC);

    const QFuture<Result<ObexResponse>> announced =
        m_obex->socketPut(QStringLiteral("\\sdcard\\up.bin"), quint64(data.size()), modified);
    QVERIFY(waitFor(announced));
    QVERIFY(announced.result().hasValue());
    QCOMPARE(announced.result().value().name, QStringLiteral("up.bin"));
    const std::optional<quint16> port =
        ObexClient::sideChannelPort(announced.result().value().appParams);
    QVERIFY(port.has_value());

    QTcpSocket side;
    QSignalSpy connected(&side, &QTcpSocket::connected);
    side.connectToHost(QHostAddress::LocalHost, *port);
    QVERIFY(connected.wait());
    side.write(data);
    QSignalSpy closed(&side, &QTcpSocket::disconnected);
    QVERIFY(closed.wait());

    const QFuture<Result<void>> status = m_obex->socketStatus();
    QVERIFY(waitFor(status));
    QVERIFY(status.result().hasValue());
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\up.bin")).value().data, data);
    QCOMPARE(m_phone->file(QStringLiteral("\\sdcard\\up.bin")).value().modified, modified);
}

void TestObexClient::socketDownloadComesOverTheSideChannel()
{
    const QByteArray data(150000, 'd');
    m_phone->addFile(QStringLiteral("\\sdcard\\big.bin"), data, QDateTime::currentDateTimeUtc());
    QVERIFY(connectTo(ObexClient::Target::FileBrowser));

    const QFuture<Result<ObexResponse>> announced =
        m_obex->socketGet(QStringLiteral("\\sdcard\\big.bin"));
    QVERIFY(waitFor(announced));
    QVERIFY(announced.result().hasValue());
    QCOMPARE(announced.result().value().length, std::optional<quint64>(quint64(data.size())));
    const std::optional<quint16> port =
        ObexClient::sideChannelPort(announced.result().value().appParams);
    QVERIFY(port.has_value());

    QTcpSocket side;
    QByteArray received;
    connect(&side, &QTcpSocket::readyRead, this,
            [&side, &received]() { received += side.readAll(); });
    side.connectToHost(QHostAddress::LocalHost, *port);
    QTRY_COMPARE_WITH_TIMEOUT(received.size(), data.size(), 10000);

    const QFuture<Result<void>> status = m_obex->socketStatus();
    QVERIFY(waitFor(status));
    QVERIFY(status.result().hasValue());
    QCOMPARE(received, data);
}

void TestObexClient::lostConnectionFailsOpenRequests()
{
    m_phone->setObjects(QStringLiteral("notes"), manyNotes(1000));
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));
    QSignalSpy progress(m_obex.get(), &ObexClient::progress);

    const QFuture<Result<ObexResponse>> dump = m_obex->get(QStringLiteral("telecom/notes.vnt"));
    const QFuture<Result<ObexResponse>> queued =
        m_obex->get(QStringLiteral("telecom/notes/info.log"));
    QVERIFY(progress.wait());
    m_phone->dropConnection();

    QVERIFY(waitFor(queued));
    QCOMPARE(dump.result().error().code(), ErrorCode::ConnectionClosed);
    QCOMPARE(queued.result().error().code(), ErrorCode::ConnectionClosed);
}

// OBEX answers carry nothing that ties them to a request. After a timeout the
// channel used to stay open: the next request went out and took the late
// answer of the first one as its own.
void TestObexClient::timeoutEndsTheObexSession()
{
    m_phone->config().unansweredObex.insert(QStringLiteral("telecom/pb/info.log"));
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));
    QSignalSpy fatal(m_session.get(), &AtSession::fatal);

    const QFuture<Result<ObexResponse>> silent =
        m_obex->get(QStringLiteral("telecom/pb/info.log"), {}, std::chrono::milliseconds(200));
    const QFuture<Result<ObexResponse>> next =
        m_obex->get(QStringLiteral("telecom/pb/luid/cc.log"));
    QVERIFY(waitFor(next));

    QVERIFY(waitFor(silent));
    QCOMPARE(silent.result().error().code(), ErrorCode::Timeout);
    QCOMPARE(next.result().error().code(), ErrorCode::ConnectionClosed);
    QVERIFY(m_session->isEnded());
    QCOMPARE(fatal.count(), 1);
    QCOMPARE(fatal.first().first().value<Error>().code(), ErrorCode::Timeout);
    QVERIFY(!m_phone->receivedCommands().contains(
        QStringLiteral("OBEX GET telecom/pb/luid/cc.log")));
    QTRY_VERIFY(!m_transport->isOpen());
}

// The bytes behind the DISCONNECT answer go to the AT session when OBEX mode
// ends. A connection-level line among them ended the session at once and
// emptied the request list under the DISCONNECT that was still being handled,
// which then was read through a null pointer.
void TestObexClient::connectionChangeRightAfterDisconnectEndsTheSessionCleanly()
{
    QVERIFY(connectTo(ObexClient::Target::IrmcSync));
    m_phone->config().afterDisconnect = QByteArrayLiteral("CONNECTION CHANGE\r\n");
    QSignalSpy fatal(m_session.get(), &AtSession::fatal);

    const QFuture<Result<void>> disconnected = m_obex->disconnectTarget();
    QVERIFY(waitFor(disconnected));

    QVERIFY(disconnected.result().hasValue());
    QVERIFY(!m_obex->isConnected());
    QTRY_COMPARE(fatal.count(), 1);
    QCOMPARE(fatal.first().first().value<Error>().code(), ErrorCode::ConnectionClosed);
    QVERIFY(m_session->isEnded());
}

void TestObexClient::phoneExceptionKeepsOnlyItsClassName_data()
{
    QTest::addColumn<QByteArray>("trace");
    QTest::addColumn<QString>("kept");

    QTest::newRow("message with a path")
        << QByteArrayLiteral("java.io.FileNotFoundException: /sdcard/Private/holiday.jpg (No "
                             "such file)\r\n\tat java.io.FileInputStream.open(Native Method)")
        << QStringLiteral("java.io.FileNotFoundException");
    QTest::newRow("class only") << QByteArrayLiteral("java.lang.IllegalStateException")
                                << QStringLiteral("java.lang.IllegalStateException");
    QTest::newRow("no class name")
        << QByteArrayLiteral("/sdcard/Private/holiday.jpg: broken") << QString();
}

// The phone puts the Java exception into the answer, and its message can name
// files on the phone. That text went into the error message and from there
// into the log.
void TestObexClient::phoneExceptionKeepsOnlyItsClassName()
{
    QFETCH(QByteArray, trace);
    QFETCH(QString, kept);
    QVERIFY(connectTo(ObexClient::Target::FileBrowser));
    m_phone->setFailNext(0xD0, trace);

    const QFuture<Result<ObexResponse>> answer = m_obex->get(QStringLiteral("a.jpg"));
    QVERIFY(waitFor(answer));

    QCOMPARE(answer.result().error().code(), ErrorCode::ProtocolError);
    const QString message = answer.result().error().message();
    QVERIFY2(message.contains(kept), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("holiday")), qPrintable(message));
    QVERIFY2(!message.contains(QStringLiteral("Native")), qPrintable(message));
}

void TestObexClient::refusedConnectReportsItsMappedError_data()
{
    QTest::addColumn<int>("response");
    QTest::addColumn<int>("code");

    QTest::newRow("unauthorized") << 0xC1 << int(ErrorCode::PermissionDenied);
    QTest::newRow("forbidden") << 0xC3 << int(ErrorCode::AccessDenied);
    QTest::newRow("not implemented") << 0xD1 << int(ErrorCode::NotSupported);
}

// The phone refuses a CONNECT with a bare response code. That answer was read
// with the CONNECT layout, failed on its length and hid the code.
void TestObexClient::refusedConnectReportsItsMappedError()
{
    QFETCH(int, response);
    QFETCH(int, code);
    m_phone->config().connectRefusal = quint8(response);

    const QFuture<Result<void>> connected = m_obex->connectTarget(ObexClient::Target::IrmcSync);
    QVERIFY(waitFor(connected));

    QVERIFY(!connected.result().hasValue());
    QCOMPARE(int(connected.result().error().code()), code);
    QVERIFY(!m_obex->isConnected());
}

QTEST_GUILESS_MAIN(TestObexClient)

#include "tst_obexclient.moc"
