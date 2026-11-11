#include "core/protocol/AtSession.h"
#include "core/protocol/TcpTransport.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::protocol;
using qpm::test::FakePhone;
using qpm::test::waitFor;

class TestAtSession : public QObject
{
    Q_OBJECT

public:
    explicit TestAtSession(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void queuedCommandsGoOutOneAtATimeInOrder();
    void unsolicitedLineInsideAReplyIsReportedSeparately();
    void linesBeforeErrorDetermineTheStatus_data();
    void linesBeforeErrorDetermineTheStatus();
    void longTextCommandGetsATrailingComma();
    void silentPhoneTimesOutAndTheNextCommandStillWorks();
    void aliveLinesDoNotKeepAnUnansweredCommandAlive();
    void cancelledCommandIsNeverSent();
    void accessDeniedEndsTheSession();
    void droppedConnectionCompletesOpenCommands();

private:
    [[nodiscard]] bool open();

    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<TcpTransport> m_transport;
    std::unique_ptr<AtSession> m_session;
};

void TestAtSession::init()
{
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_transport = std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), m_phone->port());
    m_session = std::make_unique<AtSession>(m_transport.get());
}

void TestAtSession::cleanup()
{
    m_session.reset();
    m_transport.reset();
    m_phone.reset();
}

bool TestAtSession::open()
{
    QSignalSpy opened(m_transport.get(), &Transport::opened);
    m_transport->open();
    return opened.wait();
}

void TestAtSession::queuedCommandsGoOutOneAtATimeInOrder()
{
    QVERIFY(open());

    const QFuture<AtReply> model = m_session->send(QStringLiteral("ATI"));
    const QFuture<AtReply> maker = m_session->send(QStringLiteral("AT+CGMI"));
    const QFuture<AtReply> battery = m_session->send(QStringLiteral("AT+CBC"));

    QVERIFY(waitFor(battery));
    QVERIFY(model.isFinished() && maker.isFinished());
    QCOMPARE(model.result().status, AtReply::Status::Ok);
    QCOMPARE(model.result().lines, QStringList{QStringLiteral("Example Phone X1")});
    QCOMPARE(maker.result().lines, QStringList{QStringLiteral("Example")});
    QCOMPARE(battery.result().lines, QStringList{QStringLiteral("+CBC: 0,72")});
    QCOMPARE(
        m_phone->receivedCommands(),
        (QStringList{QStringLiteral("ATI"), QStringLiteral("AT+CGMI"), QStringLiteral("AT+CBC")}));
    QVERIFY2(m_phone->protocolErrors().isEmpty(), qPrintable(m_phone->protocolErrors().join(u';')));
}

void TestAtSession::unsolicitedLineInsideAReplyIsReportedSeparately()
{
    m_phone->config().scriptedReplies.insert(
        QStringLiteral("AT*EJAVA=1"),
        {QByteArrayLiteral("AT*EJAVA=1\r\n*EJAVACOUNT: 1\r\n"),
         QByteArrayLiteral("*ALIVE: 0,55,0\r\n+CMTI: \"ME\",12\r\n"),
         QByteArrayLiteral("*EJAVA: \"App\",\"\",\"1.0\",1,com.example.app\r\nOK\r\n")});
    QVERIFY(open());
    QSignalSpy unsolicited(m_session.get(), &AtSession::unsolicited);

    const QFuture<AtReply> apps = m_session->send(QStringLiteral("AT*EJAVA=1"));
    QVERIFY(waitFor(apps));

    QCOMPARE(apps.result().status, AtReply::Status::Ok);
    QCOMPARE(apps.result().lines,
             (QStringList{QStringLiteral("*EJAVACOUNT: 1"),
                          QStringLiteral("*EJAVA: \"App\",\"\",\"1.0\",1,com.example.app")}));
    QCOMPARE(unsolicited.count(), 2);
    QCOMPARE(unsolicited.at(0).first().toString(), QStringLiteral("*ALIVE: 0,55,0"));
    QCOMPARE(unsolicited.at(1).first().toString(), QStringLiteral("+CMTI: \"ME\",12"));
}

void TestAtSession::linesBeforeErrorDetermineTheStatus_data()
{
    QTest::addColumn<QString>("command");
    QTest::addColumn<QByteArray>("reply");
    QTest::addColumn<int>("status");
    QTest::addColumn<QString>("detail");

    QTest::newRow("dump") << QStringLiteral("AT+CALA?")
                          << QByteArrayLiteral("AT+CALA?\r\nDUMP: java.lang.Exception: Alarm "
                                               "Provider not found\r\nERROR\r\n")
                          << int(AtReply::Status::Dump)
                          << QStringLiteral("java.lang.Exception: Alarm Provider not found");
    QTest::newRow("permission") << QStringLiteral("AT*SIM")
                                << QByteArrayLiteral(
                                       "AT*SIM\r\n*SIM: PERMISSION DENIED\r\nERROR\r\n")
                                << int(AtReply::Status::PermissionDenied)
                                << QStringLiteral("PERMISSION DENIED");
    QTest::newRow("not supported")
        << QStringLiteral("AT*K=?") << QByteArrayLiteral("AT*K=?\r\n*K: NOT SUPPORTED\r\nERROR\r\n")
        << int(AtReply::Status::NotSupported) << QStringLiteral("NOT SUPPORTED");
    QTest::newRow("listener disabled")
        << QStringLiteral("AT*NOTIFY=2")
        << QByteArrayLiteral("AT*NOTIFY=2\r\n*NOTIFY: DISABLED\r\nERROR\r\n")
        << int(AtReply::Status::PermissionDenied) << QStringLiteral("DISABLED");
    QTest::newRow("cms error") << QStringLiteral("AT+CPBR=5")
                               << QByteArrayLiteral("AT+CPBR=5\r\n+CMS ERROR: 321\r\n")
                               << int(AtReply::Status::CmsError) << QStringLiteral("321");
    QTest::newRow("plain error") << QStringLiteral("AT+UNKNOWN")
                                 << QByteArrayLiteral("AT+UNKNOWN\r\nERROR\r\n")
                                 << int(AtReply::Status::Error) << QString();
}

void TestAtSession::linesBeforeErrorDetermineTheStatus()
{
    QFETCH(QString, command);
    QFETCH(QByteArray, reply);
    QFETCH(int, status);
    QFETCH(QString, detail);
    m_phone->config().scriptedReplies.insert(command, {reply});
    QVERIFY(open());

    const QFuture<AtReply> answer = m_session->send(command);
    QVERIFY(waitFor(answer));

    QCOMPARE(int(answer.result().status), status);
    QCOMPARE(answer.result().detail, detail);
}

void TestAtSession::longTextCommandGetsATrailingComma()
{
    QVERIFY(open());

    const QFuture<AtReply> clipboard = m_session->send(QStringLiteral("AT*CLIPBOARD=hello"));
    QVERIFY(waitFor(clipboard));

    QCOMPARE(clipboard.result().status, AtReply::Status::Ok);
    QCOMPARE(m_phone->receivedCommands(), QStringList{QStringLiteral("AT*CLIPBOARD=hello,")});
    QVERIFY(m_phone->protocolErrors().isEmpty());
}

void TestAtSession::silentPhoneTimesOutAndTheNextCommandStillWorks()
{
    m_phone->config().unanswered.insert(QStringLiteral("AT+SLOW"));
    QVERIFY(open());

    const QFuture<AtReply> slow =
        m_session->send(QStringLiteral("AT+SLOW"), std::chrono::milliseconds(200));
    const QFuture<AtReply> next = m_session->send(QStringLiteral("ATI"));

    QVERIFY(waitFor(next));
    QCOMPARE(slow.result().status, AtReply::Status::Timeout);
    QCOMPARE(next.result().status, AtReply::Status::Ok);
}

// The phone sends "*ALIVE" every ten seconds, and the command timeout is 15
// seconds. Every received chunk used to restart that timeout, so a command the
// phone never answered waited for good on a link that carried keep-alives.
void TestAtSession::aliveLinesDoNotKeepAnUnansweredCommandAlive()
{
    m_phone->config().unanswered.insert(QStringLiteral("AT+SLOW"));
    m_phone->config().alivePeriod = std::chrono::milliseconds(50);
    QVERIFY(open());
    QSignalSpy unsolicited(m_session.get(), &AtSession::unsolicited);

    const QFuture<AtReply> slow =
        m_session->send(QStringLiteral("AT+SLOW"), std::chrono::milliseconds(300));
    QVERIFY(waitFor(slow, 3000));

    QCOMPARE(slow.result().status, AtReply::Status::Timeout);
    QVERIFY(unsolicited.count() >= 2);
}

void TestAtSession::cancelledCommandIsNeverSent()
{
    m_phone->config().unanswered.insert(QStringLiteral("AT+SLOW"));
    QVERIFY(open());

    const QFuture<AtReply> slow =
        m_session->send(QStringLiteral("AT+SLOW"), std::chrono::milliseconds(200));
    QFuture<AtReply> dropped = m_session->send(QStringLiteral("ATI"));
    const QFuture<AtReply> last = m_session->send(QStringLiteral("AT+CGMI"));
    dropped.cancel();

    QVERIFY(waitFor(last));
    QVERIFY(dropped.isCanceled());
    QCOMPARE(m_phone->receivedCommands(),
             (QStringList{QStringLiteral("AT+SLOW"), QStringLiteral("AT+CGMI")}));
}

void TestAtSession::accessDeniedEndsTheSession()
{
    m_phone->config().pin = QStringLiteral("2580");
    QVERIFY(open());
    QSignalSpy fatal(m_session.get(), &AtSession::fatal);

    const QFuture<AtReply> model = m_session->send(QStringLiteral("ATI"));
    QVERIFY(waitFor(model));

    QCOMPARE(model.result().status, AtReply::Status::AccessDenied);
    QCOMPARE(fatal.count(), 1);
    QCOMPARE(fatal.first().first().value<Error>().code(), ErrorCode::AccessDenied);
    QVERIFY(m_session->isEnded());
    const QFuture<AtReply> later = m_session->send(QStringLiteral("AT+CBC"));
    QVERIFY(waitFor(later));
    QCOMPARE(later.result().status, AtReply::Status::Closed);
}

void TestAtSession::droppedConnectionCompletesOpenCommands()
{
    m_phone->config().unanswered.insert(QStringLiteral("AT+SLOW"));
    QVERIFY(open());
    QSignalSpy sent(m_phone.get(), &FakePhone::commandReceived);

    const QFuture<AtReply> slow = m_session->send(QStringLiteral("AT+SLOW"));
    const QFuture<AtReply> queued = m_session->send(QStringLiteral("ATI"));
    QVERIFY(sent.wait());
    m_phone->dropConnection();

    QVERIFY(waitFor(queued));
    QCOMPARE(slow.result().status, AtReply::Status::Closed);
    QCOMPARE(queued.result().status, AtReply::Status::Closed);
}

QTEST_GUILESS_MAIN(TestAtSession)

#include "tst_atsession.moc"
