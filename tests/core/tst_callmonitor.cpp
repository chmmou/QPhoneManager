#include "core/codec/PhoneVCard.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/CallMonitor.h"
#include "core/service/ContactsService.h"
#include "core/store/ContactStore.h"
#include "core/store/ProfileStore.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::CallMonitor;
using qpm::core::service::ContactsService;
using qpm::core::store::ContactStore;
using qpm::core::store::ProfileStore;
using qpm::test::FakePhone;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 15000;
using State = CallMonitor::State;

QByteArray contactWith(const QString &name, const QString &number)
{
    Contact contact;
    contact.luid = 1;
    contact.givenName = name;
    contact.displayName = name;
    ContactValue phone;
    phone.dataId = 11;
    phone.type = phoneType::mobile;
    phone.value = number;
    contact.phones = {phone};
    return encodeAsPhoneSendsIt(contact);
}

} // namespace

// The call monitor against the fake phone: the states the phone reports, the
// caller's name from the contacts, answering, ending, muting and the speaker,
// and reports that are not taken.
class TestCallMonitor : public QObject
{
    Q_OBJECT

public:
    explicit TestCallMonitor(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void ringingCallShowsNumberAndContactName();
    void answeredCallGoesOnAndEndsWithHangingUp();
    void outgoingCallWithoutNumberKeepsItsStates();
    void hangUpFallsBackToTheStandardCommand();
    void muteAndSpeakerFollowThePhoneAndEndWithTheCall();
    void refusedSwitchKeepsTheState();
    void answerWithoutARingingCallSendsNothing();
    void malformedReportsAreIgnored_data();
    void malformedReportsAreIgnored();
    void lostConnectionEndsTheCall();

private:
    [[nodiscard]] bool everythingSentIsAnswered();

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<ProfileStore> m_profile;
    std::shared_ptr<ContactStore> m_contacts;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<CallMonitor> m_monitor;
};

void TestCallMonitor::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_profile = std::make_unique<ProfileStore>(m_dir->path(), QStringLiteral("dev-1"));
    QVERIFY(m_profile->open().hasValue());
    m_contacts = std::make_shared<ContactStore>(m_profile.get());
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setObjects(QStringLiteral("pb"), {{1, contactWith(QStringLiteral("Jörg Weiß"),
                                                               QStringLiteral("0151 2000005"))}});
    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    ContactsService contacts(m_connection.get());
    contacts.setStore(m_contacts);
    const QFuture<Result<store::ContactChanges>> synced = contacts.sync();
    QVERIFY(waitFor(synced, timeoutMs));
    QVERIFY(synced.result().hasValue());
    m_monitor = std::make_unique<CallMonitor>(m_connection.get());
    m_monitor->setContactStore(m_contacts);
    // The connection asks for the status again after the sync.
    QVERIFY(everythingSentIsAnswered());
    m_phone->clearLog();
}

void TestCallMonitor::cleanup()
{
    if (m_phone)
        QCOMPARE(m_phone->protocolErrors(), QStringList());
    m_monitor.reset();
    m_connection.reset();
    m_phone.reset();
    m_contacts.reset();
    m_profile.reset();
    m_dir.reset();
}

// The phone writes its own lines before the answer that follows them.
bool TestCallMonitor::everythingSentIsAnswered()
{
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return false;
    const QFuture<protocol::AtReply> reply = session->send(QStringLiteral("AT+CSQ"));
    return waitFor(reply, timeoutMs) && reply.result().status == protocol::AtReply::Status::Ok;
}

void TestCallMonitor::ringingCallShowsNumberAndContactName()
{
    QSignalSpy changed(m_monitor.get(), &CallMonitor::callChanged);

    m_phone->setCallState(6, QStringLiteral("+491512000005"));

    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Ringing, timeoutMs);
    QCOMPARE(m_monitor->number(), QStringLiteral("+491512000005"));
    QCOMPARE(m_monitor->name(), QStringLiteral("Jörg Weiß"));
    QCOMPARE(changed.count(), 1);
    m_phone->setCallState(6, QStringLiteral("+4930999999"));
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->number(), QStringLiteral("+4930999999"), timeoutMs);
    QVERIFY(m_monitor->name().isEmpty());
}

void TestCallMonitor::answeredCallGoesOnAndEndsWithHangingUp()
{
    m_phone->setCallState(6, QStringLiteral("01512000005"));
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Ringing, timeoutMs);

    const QFuture<Result<void>> answered = m_monitor->answer();
    QVERIFY(waitFor(answered, timeoutMs));
    QVERIFY(answered.result().hasValue());
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Active, timeoutMs);
    QCOMPARE(m_monitor->name(), QStringLiteral("Jörg Weiß"));
    const QFuture<Result<void>> ended = m_monitor->hangUp();
    QVERIFY(waitFor(ended, timeoutMs));

    QVERIFY(ended.result().hasValue());
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Idle, timeoutMs);
    QVERIFY(m_monitor->number().isEmpty());
    QCOMPARE(m_phone->receivedCommands(),
             (QStringList{QStringLiteral("ATA"), QStringLiteral("ATH")}));
}

void TestCallMonitor::outgoingCallWithoutNumberKeepsItsStates()
{
    QSignalSpy changed(m_monitor.get(), &CallMonitor::callChanged);

    m_phone->setCallState(1, QStringLiteral("01512000005"));
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Dialling, timeoutMs);
    // The phone may report a call that goes on without its number.
    m_phone->setCallState(3, QString());
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Active, timeoutMs);
    QCOMPARE(m_monitor->number(), QStringLiteral("01512000005"));
    m_phone->setCallState(0, QString());
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Idle, timeoutMs);
    QCOMPARE(changed.count(), 3);
}

void TestCallMonitor::hangUpFallsBackToTheStandardCommand()
{
    m_phone->config().scriptedReplies.insert(QStringLiteral("ATH"),
                                             {QByteArrayLiteral("ATH\r\nERROR\r\n")});
    m_phone->setCallState(3, QStringLiteral("01512000005"));
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Active, timeoutMs);

    const QFuture<Result<void>> ended = m_monitor->hangUp();
    QVERIFY(waitFor(ended, timeoutMs));

    QVERIFY(ended.result().hasValue());
    QCOMPARE(m_phone->receivedCommands(),
             (QStringList{QStringLiteral("ATH"), QStringLiteral("AT+CHUP")}));
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Idle, timeoutMs);
}

void TestCallMonitor::muteAndSpeakerFollowThePhoneAndEndWithTheCall()
{
    m_phone->setCallState(3, QStringLiteral("01512000005"));
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Active, timeoutMs);
    QSignalSpy muted(m_monitor.get(), &CallMonitor::mutedChanged);
    QSignalSpy speaker(m_monitor.get(), &CallMonitor::speakerChanged);

    const QFuture<Result<void>> mute = m_monitor->setMuted(true);
    const QFuture<Result<void>> loud = m_monitor->setSpeaker(true);
    QVERIFY(waitFor(mute, timeoutMs) && waitFor(loud, timeoutMs));

    QVERIFY(mute.result().hasValue() && loud.result().hasValue());
    QVERIFY(m_monitor->isMuted() && m_monitor->isSpeakerOn());
    QVERIFY(m_phone->callMuted() && m_phone->speakerOn());
    QCOMPARE(m_phone->receivedCommands(),
             (QStringList{QStringLiteral("AT+CMUT=1"), QStringLiteral("AT*SESP=1")}));
    m_phone->setCallState(0, QString());
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Idle, timeoutMs);
    QVERIFY(!m_monitor->isMuted() && !m_monitor->isSpeakerOn());
    QCOMPARE(muted.count(), 2);
    QCOMPARE(speaker.count(), 2);
}

void TestCallMonitor::refusedSwitchKeepsTheState()
{
    m_phone->config().scriptedReplies.insert(QStringLiteral("AT+CMUT=1"),
                                             {QByteArrayLiteral("AT+CMUT=1\r\nERROR\r\n")});
    m_phone->setCallState(3, QStringLiteral("01512000005"));
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Active, timeoutMs);

    const QFuture<Result<void>> mute = m_monitor->setMuted(true);
    QVERIFY(waitFor(mute, timeoutMs));

    QCOMPARE(mute.result().error().code(), ErrorCode::Rejected);
    QVERIFY(!m_monitor->isMuted());
}

void TestCallMonitor::answerWithoutARingingCallSendsNothing()
{
    const QFuture<Result<void>> answered = m_monitor->answer();
    QVERIFY(waitFor(answered));

    QCOMPARE(answered.result().error().code(), ErrorCode::InvalidInput);
    QVERIFY(m_phone->receivedCommands().isEmpty());
}

void TestCallMonitor::malformedReportsAreIgnored_data()
{
    QTest::addColumn<QByteArray>("line");

    QTest::newRow("letters in the number") << QByteArray("*ECAV: 1,6,1,0,0,\"0151abc\",129");
    QTest::newRow("plus inside") << QByteArray("*ECAV: 1,6,1,0,0,\"01+51\",129");
    QTest::newRow("number too long") << ("*ECAV: 1,6,1,0,0,\"" + QByteArray(40, '1') + "\",129");
    QTest::newRow("state no number") << QByteArray("*ECAV: 1,x,1,0,0,\"0151\",129");
    QTest::newRow("unknown state") << QByteArray("*ECAV: 1,4,1,0,0,\"0151\",129");
    QTest::newRow("too few fields") << QByteArray("*ECAV: 1,6");
}

void TestCallMonitor::malformedReportsAreIgnored()
{
    QFETCH(QByteArray, line);
    QSignalSpy changed(m_monitor.get(), &CallMonitor::callChanged);

    m_phone->sendUnsolicited(line);
    QVERIFY(everythingSentIsAnswered());

    QCOMPARE(changed.count(), 0);
    QCOMPARE(m_monitor->state(), State::Idle);
}

void TestCallMonitor::lostConnectionEndsTheCall()
{
    m_phone->setCallState(6, QStringLiteral("01512000005"));
    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Ringing, timeoutMs);

    m_phone->dropConnection();

    QTRY_COMPARE_WITH_TIMEOUT(m_monitor->state(), State::Idle, timeoutMs);
    const QFuture<Result<void>> ended = m_monitor->hangUp();
    QVERIFY(waitFor(ended));
    QCOMPARE(ended.result().error().code(), ErrorCode::ConnectionClosed);
}

QTEST_GUILESS_MAIN(TestCallMonitor)

#include "tst_callmonitor.moc"
