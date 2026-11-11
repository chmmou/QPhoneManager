#include "CommandGuard.h"

#include "core/protocol/AtSession.h"
#include "core/protocol/ObexPacket.h"
#include "core/protocol/TcpTransport.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::protocol;
using qpm::test::CommandGuard;
using qpm::test::FakePhone;
using qpm::test::waitFor;

namespace {

QByteArray obexPut(const QString &name, const QByteArray &body)
{
    ObexPacket packet;
    packet.code = obex::opPutFinal;
    packet.connectionId = 1;
    packet.name = name;
    packet.bodyKind = ObexPacket::BodyKind::EndOfBody;
    packet.body = body;
    return packet.encode().value();
}

} // namespace

class TestCommandGuard : public QObject
{
    Q_OBJECT

public:
    explicit TestCommandGuard(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void callAndMessageCommandsAreBlocked_data();
    void callAndMessageCommandsAreBlocked();
    void harmlessCommandsPass_data();
    void harmlessCommandsPass();
    void schedulingAMessageOverObexIsBlocked();
    void blockedCommandNeverReachesThePhone();
};

void TestCommandGuard::callAndMessageCommandsAreBlocked_data()
{
    QTest::addColumn<QString>("command");

    QTest::newRow("dial") << QStringLiteral("ATD+49301234567;");
    QTest::newRow("dial with sim") << QStringLiteral("ATD+49301234567;,1");
    QTest::newRow("answer") << QStringLiteral("ATA");
    QTest::newRow("hang up") << QStringLiteral("ATH");
    QTest::newRow("hang up, other form") << QStringLiteral("AT+CHUP");
    QTest::newRow("lower case dial") << QStringLiteral("atd123;");
    QTest::newRow("send text") << QStringLiteral("AT+CMGS=+49301234567,Hello,1,1");
    QTest::newRow("send ping") << QStringLiteral("AT+CMGS=+49301234567,,2");
    QTest::newRow("send pdu") << QStringLiteral("AT+CMGS=23");
    QTest::newRow("notification action") << QStringLiteral("AT*NOTIACTION=1,tag,com.example,Reply");
}

void TestCommandGuard::callAndMessageCommandsAreBlocked()
{
    QFETCH(QString, command);

    QVERIFY(CommandGuard::blockReason(command).has_value());
    QVERIFY(CommandGuard::blockReasonForFrame(command.toUtf8() + "\r\n").has_value());
}

void TestCommandGuard::harmlessCommandsPass_data()
{
    QTest::addColumn<QString>("command");

    QTest::newRow("capability probe") << QStringLiteral("AT+CMGS=?");
    QTest::newRow("identity") << QStringLiteral("ATI");
    QTest::newRow("battery") << QStringLiteral("AT+CBC");
    QTest::newRow("version") << QStringLiteral("AT*APPVERSION=2.3.0");
    QTest::newRow("call log") << QStringLiteral("AT+CPBR=,");
    QTest::newRow("obex") << QStringLiteral("AT+CPROT=0");
}

void TestCommandGuard::harmlessCommandsPass()
{
    QFETCH(QString, command);

    QVERIFY(!CommandGuard::blockReason(command).has_value());
}

void TestCommandGuard::schedulingAMessageOverObexIsBlocked()
{
    QVERIFY(CommandGuard::blockReasonForFrame(
                obexPut(QStringLiteral("telecom/msg/luid/.vmg"), "BEGIN:VMESSAGE\r\n"))
                .has_value());
    QVERIFY(!CommandGuard::blockReasonForFrame(
                 obexPut(QStringLiteral("telecom/msg/setflags"), "IncludeSheduleSMS"))
                 .has_value());
    QVERIFY(!CommandGuard::blockReasonForFrame(
                 obexPut(QStringLiteral("telecom/notes/luid/.vnt"), "BEGIN:VNOTE\r\n"))
                 .has_value());
}

void TestCommandGuard::blockedCommandNeverReachesThePhone()
{
    FakePhone phone;
    QVERIFY(phone.listen());
    CommandGuard guard(std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), phone.port()));
    AtSession session(&guard);
    QSignalSpy opened(&guard, &Transport::opened);
    QSignalSpy failed(&guard, &Transport::failed);
    guard.open();
    QVERIFY(opened.wait());

    const QFuture<AtReply> model = session.send(QStringLiteral("ATI"));
    QVERIFY(waitFor(model));
    const QFuture<AtReply> call = session.send(QStringLiteral("ATD+49301234567;"));
    QVERIFY(waitFor(call));

    QCOMPARE(call.result().status, AtReply::Status::Closed);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().first().value<Error>().code(), ErrorCode::Rejected);
    QCOMPARE(guard.blocked().size(), 1);
    QCOMPARE(phone.receivedCommands(), QStringList{QStringLiteral("ATI")});
}

QTEST_GUILESS_MAIN(TestCommandGuard)

#include "tst_commandguard.moc"
