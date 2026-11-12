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

// Made-up numbers for the allow list. The real ones live outside the
// repository and are never used here.
constexpr char messageNumber[] = "+491550000001";
constexpr char callNumber[] = "+491550000002";

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
    void init();
    void cleanup();

    void callAndMessageCommandsAreBlocked_data();
    void callAndMessageCommandsAreBlocked();
    void harmlessCommandsPass_data();
    void harmlessCommandsPass();
    void schedulingAMessageOverObexIsBlocked();
    void blockedCommandNeverReachesThePhone();
    void paddedOrMixedCaseCommandsAreBlocked_data();
    void paddedOrMixedCaseCommandsAreBlocked();
    void blockedCommandBehindAHarmlessOneIsBlocked();
    void messagePutAnywhereBelowMsgIsBlocked_data();
    void messagePutAnywhereBelowMsgIsBlocked();
    void undecodablePutIsBlocked();
    void allowedNumbersAreBlockedWhileUnset();
    void messageToTheAllowedNumberPasses_data();
    void messageToTheAllowedNumberPasses();
    void callToTheAllowedNumberPasses_data();
    void callToTheAllowedNumberPasses();
    void everythingElseStaysBlockedWithAllowedNumbers_data();
    void everythingElseStaysBlockedWithAllowedNumbers();
    void incompleteAllowedNumberAllowsNothing_data();
    void incompleteAllowedNumberAllowsNothing();
    void appCommandsPassOnlyInTheirThreeForms_data();
    void appCommandsPassOnlyInTheirThreeForms();
    void screenCommandsPassOnlyToAnEmulator_data();
    void screenCommandsPassOnlyToAnEmulator();
    void callAndMessageCommandsStayBlockedOnAnEmulator();
    void guardOfAPhoneKeepsInputAwayFromIt();
};

// The allow list comes from the environment; every test starts without it.
void TestCommandGuard::init()
{
    qunsetenv("QPM_TEST_SMS_TO");
    qunsetenv("QPM_TEST_CALL_TO");
}

void TestCommandGuard::cleanup()
{
    qunsetenv("QPM_TEST_SMS_TO");
    qunsetenv("QPM_TEST_CALL_TO");
}

void TestCommandGuard::callAndMessageCommandsAreBlocked_data()
{
    QTest::addColumn<QString>("command");

    QTest::newRow("dial") << QStringLiteral("ATD+49301234567;");
    QTest::newRow("dial with sim") << QStringLiteral("ATD+49301234567;,1");
    QTest::newRow("answer") << QStringLiteral("ATA");
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
    QTest::newRow("hang up") << QStringLiteral("ATH");
    QTest::newRow("hang up, other form") << QStringLiteral("AT+CHUP");
    QTest::newRow("hang up, padded and mixed case") << QStringLiteral(" at+Chup\t");
}

void TestCommandGuard::harmlessCommandsPass()
{
    QFETCH(QString, command);

    QVERIFY(!CommandGuard::blockReason(command).has_value());
    QVERIFY(!CommandGuard::blockReasonForFrame(command.toUtf8() + "\r\n").has_value());
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

void TestCommandGuard::paddedOrMixedCaseCommandsAreBlocked_data()
{
    QTest::addColumn<QByteArray>("frame");

    QTest::newRow("leading space") << QByteArrayLiteral(" ATD+49301234567;\r\n");
    QTest::newRow("leading tab") << QByteArrayLiteral("\tATA\r\n");
    QTest::newRow("leading control character") << QByteArrayLiteral("\x01" "ATD123;\r\n");
    QTest::newRow("mixed case") << QByteArrayLiteral("At+CMGS=23\r\n");
    QTest::newRow("other mixed case") << QByteArrayLiteral("aTa\r\n");
}

// The phone trims a command before it reads it. The guard looked at the raw
// frame, so a leading space or a mixed-case "At" let a call through.
void TestCommandGuard::paddedOrMixedCaseCommandsAreBlocked()
{
    QFETCH(QByteArray, frame);

    QVERIFY(CommandGuard::blockReasonForFrame(frame).has_value());
}

// The phone reads line by line. A harmless first line used to decide for the
// whole frame.
void TestCommandGuard::blockedCommandBehindAHarmlessOneIsBlocked()
{
    QVERIFY(CommandGuard::blockReasonForFrame(QByteArrayLiteral("ATI\r\nATD123;\r\n"))
                .has_value());
}

void TestCommandGuard::messagePutAnywhereBelowMsgIsBlocked_data()
{
    QTest::addColumn<QString>("name");

    QTest::newRow("other root") << QStringLiteral("x/msg/luid/.vmg");
    QTest::newRow("upper case") << QStringLiteral("telecom/MSG/luid/.vmg");
    QTest::newRow("leading slash") << QStringLiteral("/msg/luid/.vmg");
    QTest::newRow("setflags elsewhere") << QStringLiteral("x/msg/setflags");
}

// The phone routes a PUT to its message handler when the name contains
// "/msg". The guard only looked below "telecom/msg/".
void TestCommandGuard::messagePutAnywhereBelowMsgIsBlocked()
{
    QFETCH(QString, name);

    QVERIFY(CommandGuard::blockReasonForFrame(obexPut(name, "BEGIN:VMESSAGE\r\n")).has_value());
}

// A PUT the guard could not decode was passed on unchecked.
void TestCommandGuard::undecodablePutIsBlocked()
{
    // A Name header that claims more bytes than the packet holds.
    const QByteArray frame = QByteArray::fromHex("82000801000900");

    QVERIFY(CommandGuard::blockReasonForFrame(frame).has_value());
}

void TestCommandGuard::allowedNumbersAreBlockedWhileUnset()
{
    const QString message =
        QStringLiteral("AT+CMGS=%1,Hello,1,1").arg(QLatin1StringView(messageNumber));
    const QString call = QStringLiteral("ATD%1;").arg(QLatin1StringView(callNumber));

    QVERIFY(CommandGuard::blockReason(message).has_value());
    QVERIFY(CommandGuard::blockReason(call).has_value());
}

void TestCommandGuard::messageToTheAllowedNumberPasses_data()
{
    QTest::addColumn<QByteArray>("allowed");
    QTest::addColumn<QString>("command");

    QTest::newRow("text") << QByteArray(messageNumber)
                          << QStringLiteral("AT+CMGS=+491550000001,Hello,1,1");
    QTest::newRow("ping") << QByteArray(messageNumber)
                          << QStringLiteral("AT+CMGS=+491550000001,,2");
    QTest::newRow("national form") << QByteArray(messageNumber)
                                   << QStringLiteral("AT+CMGS=01550000001,Hello,1,1");
    QTest::newRow("spaces") << QByteArray(messageNumber)
                            << QStringLiteral("AT+CMGS=+49 155 0000001,Hello,1,1");
    QTest::newRow("allowed in national form")
        << QByteArrayLiteral("0155 0000001") << QStringLiteral("AT+CMGS=+491550000001,Hello,1,1");
}

void TestCommandGuard::messageToTheAllowedNumberPasses()
{
    QFETCH(QByteArray, allowed);
    QFETCH(QString, command);
    qputenv("QPM_TEST_SMS_TO", allowed);

    QVERIFY(!CommandGuard::blockReason(command).has_value());
    QVERIFY(!CommandGuard::blockReasonForFrame(command.toUtf8() + "\r\n").has_value());
}

void TestCommandGuard::callToTheAllowedNumberPasses_data()
{
    QTest::addColumn<QByteArray>("allowed");
    QTest::addColumn<QString>("command");

    QTest::newRow("voice call") << QByteArray(callNumber) << QStringLiteral("ATD+491550000002;");
    QTest::newRow("with sim") << QByteArray(callNumber) << QStringLiteral("ATD+491550000002;,1");
    QTest::newRow("national form") << QByteArray(callNumber) << QStringLiteral("ATD01550000002;");
    QTest::newRow("lower case") << QByteArray(callNumber) << QStringLiteral("atd+491550000002;");
    QTest::newRow("allowed in national form")
        << QByteArrayLiteral("0155 0000002") << QStringLiteral("ATD+491550000002;");
}

void TestCommandGuard::callToTheAllowedNumberPasses()
{
    QFETCH(QByteArray, allowed);
    QFETCH(QString, command);
    qputenv("QPM_TEST_CALL_TO", allowed);

    QVERIFY(!CommandGuard::blockReason(command).has_value());
    QVERIFY(!CommandGuard::blockReasonForFrame(command.toUtf8() + "\r\n").has_value());
}

void TestCommandGuard::everythingElseStaysBlockedWithAllowedNumbers_data()
{
    QTest::addColumn<QString>("command");

    QTest::newRow("call to another number") << QStringLiteral("ATD+491550000003;");
    QTest::newRow("call to the message number") << QStringLiteral("ATD+491550000001;");
    QTest::newRow("call with a longer number") << QStringLiteral("ATD+4915500000021;");
    QTest::newRow("call without semicolon") << QStringLiteral("ATD+491550000002");
    QTest::newRow("call with trailing text") << QStringLiteral("ATD+491550000002;;");
    QTest::newRow("call with prefix code") << QStringLiteral("ATD*31#+491550000002;");
    QTest::newRow("redial") << QStringLiteral("ATDL");
    QTest::newRow("message to another number")
        << QStringLiteral("AT+CMGS=+491550000003,Hello,1,1");
    QTest::newRow("message to the call number")
        << QStringLiteral("AT+CMGS=+491550000002,Hello,1,1");
    QTest::newRow("message to two numbers")
        << QStringLiteral("AT+CMGS=+491550000001;+491550000003,Hello,1,1");
    QTest::newRow("quoted number") << QStringLiteral("AT+CMGS=\"+491550000001\",Hello,1,1");
    QTest::newRow("pdu") << QStringLiteral("AT+CMGS=23");
    QTest::newRow("answer") << QStringLiteral("ATA");
    QTest::newRow("notification action")
        << QStringLiteral("AT*NOTIACTION=1,tag,com.example,Reply");
}

void TestCommandGuard::everythingElseStaysBlockedWithAllowedNumbers()
{
    QFETCH(QString, command);
    qputenv("QPM_TEST_SMS_TO", messageNumber);
    qputenv("QPM_TEST_CALL_TO", callNumber);

    QVERIFY(CommandGuard::blockReason(command).has_value());
    QVERIFY(CommandGuard::blockReasonForFrame(obexPut(QStringLiteral("telecom/msg/luid/.vmg"),
                                                      "BEGIN:VMESSAGE\r\n"))
                .has_value());
}

void TestCommandGuard::incompleteAllowedNumberAllowsNothing_data()
{
    QTest::addColumn<QByteArray>("allowed");
    QTest::addColumn<QString>("command");

    QTest::newRow("country code only") << QByteArrayLiteral("+49") << QStringLiteral("ATD0;");
    QTest::newRow("short code") << QByteArrayLiteral("112") << QStringLiteral("ATD112;");
    QTest::newRow("empty") << QByteArray() << QStringLiteral("ATD;");
    QTest::newRow("letters") << QByteArrayLiteral("abc") << QStringLiteral("ATDabc;");
}

// An allowed number has to be written in full. A value that names no full
// number would otherwise open the guard to an exchange or emergency code.
void TestCommandGuard::incompleteAllowedNumberAllowsNothing()
{
    QFETCH(QByteArray, allowed);
    QFETCH(QString, command);
    qputenv("QPM_TEST_CALL_TO", allowed);

    QVERIFY(CommandGuard::blockReason(command).has_value());
}

void TestCommandGuard::appCommandsPassOnlyInTheirThreeForms_data()
{
    QTest::addColumn<QString>("command");
    QTest::addColumn<bool>("passes");

    QTest::newRow("list") << QStringLiteral("AT*EJAVA=1") << true;
    QTest::newRow("start") << QStringLiteral("AT*EJAVA=4,com.android.settings") << true;
    QTest::newRow("removal") << QStringLiteral("AT*EJAVA=2,org.example.Test_app2") << true;
    QTest::newRow("padded list") << QStringLiteral(" at*ejava=1\t") << true;
    QTest::newRow("bare") << QStringLiteral("AT*EJAVA") << false;
    QTest::newRow("probe") << QStringLiteral("AT*EJAVA=?") << false;
    QTest::newRow("other mode") << QStringLiteral("AT*EJAVA=3,com.example") << false;
    QTest::newRow("list with argument") << QStringLiteral("AT*EJAVA=1,com.example") << false;
    QTest::newRow("start without package") << QStringLiteral("AT*EJAVA=4,") << false;
    QTest::newRow("start with two packages") << QStringLiteral("AT*EJAVA=4,a.b,c.d") << false;
    QTest::newRow("removal with a space") << QStringLiteral("AT*EJAVA=2,org example") << false;
    QTest::newRow("longer name") << QStringLiteral("AT*EJAVAX=1") << false;
}

// Only the list, a start and a removal request may reach a phone; any other
// form of the app command stays out.
void TestCommandGuard::appCommandsPassOnlyInTheirThreeForms()
{
    QFETCH(QString, command);
    QFETCH(bool, passes);

    QCOMPARE(!CommandGuard::blockReason(command).has_value(), passes);
    QCOMPARE(!CommandGuard::blockReasonForFrame(command.toUtf8() + "\r\n").has_value(), passes);
}

void TestCommandGuard::screenCommandsPassOnlyToAnEmulator_data()
{
    QTest::addColumn<QString>("command");
    QTest::addColumn<bool>("screen");

    QTest::newRow("capture probe") << QStringLiteral("AT*STARTCAPTURESERVER=?") << true;
    QTest::newRow("capture") << QStringLiteral("AT*STARTCAPTURESERVER") << true;
    QTest::newRow("touch") << QStringLiteral("AT*C=0,540,1200,1") << true;
    QTest::newRow("touch probe") << QStringLiteral("AT*C=?") << true;
    QTest::newRow("key") << QStringLiteral("AT*K=back") << true;
    QTest::newRow("key probe") << QStringLiteral("AT*K=?") << true;
    QTest::newRow("text") << QStringLiteral("AT*T=Hallo,") << true;
    QTest::newRow("selected text") << QStringLiteral("AT*SELTEXT?") << true;
    QTest::newRow("keyboard") << QStringLiteral("AT*KBRD=2,0,Hallo,") << true;
    QTest::newRow("keyboard state") << QStringLiteral("AT*KBRD?") << true;
    QTest::newRow("accessibility settings") << QStringLiteral("AT*SHOWACCSETTINGS") << true;
    QTest::newRow("padded and lower case") << QStringLiteral(" at*k=home\t") << true;
    QTest::newRow("clipboard") << QStringLiteral("AT*CLIPBOARD?") << false;
    QTest::newRow("clipboard watch") << QStringLiteral("AT*CLIPWATCH=1") << false;
    QTest::newRow("display") << QStringLiteral("AT*DISPLAY") << false;
    QTest::newRow("call log storage") << QStringLiteral("AT+CPBS=\"DC\"") << false;
}

// The capture asks the owner of the phone, and input acts on whatever it
// shows, so neither reaches a phone that is not an emulator.
void TestCommandGuard::screenCommandsPassOnlyToAnEmulator()
{
    QFETCH(QString, command);
    QFETCH(bool, screen);
    using Target = CommandGuard::Target;

    QCOMPARE(CommandGuard::blockReason(command).has_value(), screen);
    QCOMPARE(CommandGuard::blockReason(command, Target::Phone).has_value(), screen);
    QCOMPARE(
        CommandGuard::blockReasonForFrame(command.toUtf8() + "\r\n", Target::Phone).has_value(),
        screen);
    QVERIFY(!CommandGuard::blockReason(command, Target::Emulator).has_value());
    QVERIFY(!CommandGuard::blockReasonForFrame(command.toUtf8() + "\r\n", Target::Emulator)
                 .has_value());
}

void TestCommandGuard::callAndMessageCommandsStayBlockedOnAnEmulator()
{
    using Target = CommandGuard::Target;
    for (const QString &command : {QStringLiteral("ATD+49301234567;"), QStringLiteral("ATA"),
                                   QStringLiteral("AT+CMGS=+49301234567,Hello,1,1"),
                                   QStringLiteral("AT*NOTIACTION=1,tag,com.example,Reply"),
                                   QStringLiteral("AT*EJAVA=3,com.example")}) {
        QVERIFY2(CommandGuard::blockReason(command, Target::Emulator).has_value(),
                 qPrintable(command));
    }
    QVERIFY(CommandGuard::blockReasonForFrame(
                obexPut(QStringLiteral("telecom/msg/luid/.vmg"), "BEGIN:VMESSAGE\r\n"),
                Target::Emulator)
                .has_value());
}

void TestCommandGuard::guardOfAPhoneKeepsInputAwayFromIt()
{
    FakePhone phone;
    QVERIFY(phone.listen());
    CommandGuard emulatorGuard(
        std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), phone.port()),
        CommandGuard::Target::Emulator);
    AtSession emulatorSession(&emulatorGuard);
    QSignalSpy emulatorOpened(&emulatorGuard, &Transport::opened);
    emulatorGuard.open();
    QVERIFY(emulatorOpened.wait());
    const QFuture<AtReply> key = emulatorSession.send(QStringLiteral("AT*K=back"));
    QVERIFY(waitFor(key));
    QCOMPARE(key.result().status, AtReply::Status::Ok);
    QVERIFY(emulatorGuard.blocked().isEmpty());
    emulatorGuard.close();

    CommandGuard phoneGuard(
        std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), phone.port()));
    AtSession phoneSession(&phoneGuard);
    QSignalSpy phoneOpened(&phoneGuard, &Transport::opened);
    QSignalSpy failed(&phoneGuard, &Transport::failed);
    phoneGuard.open();
    QVERIFY(phoneOpened.wait());
    phone.clearLog();
    const QFuture<AtReply> capture = phoneSession.send(QStringLiteral("AT*STARTCAPTURESERVER"));
    QVERIFY(waitFor(capture));

    QCOMPARE(capture.result().status, AtReply::Status::Closed);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.first().first().value<Error>().code(), ErrorCode::Rejected);
    QCOMPARE(phoneGuard.blocked().size(), 1);
    QVERIFY(phone.receivedCommands().isEmpty());
}

QTEST_GUILESS_MAIN(TestCommandGuard)

#include "tst_commandguard.moc"
