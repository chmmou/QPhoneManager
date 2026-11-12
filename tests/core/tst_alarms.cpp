#include "core/protocol/PhoneConnection.h"
#include "core/service/AlarmService.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QTest>

#include <memory>

Q_DECLARE_METATYPE(qpm::core::service::PhoneAlarm)

using namespace qpm::core;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::AlarmService;
using qpm::core::service::PhoneAlarm;
using qpm::test::FakePhone;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 15000;

QStringList alarmCommands(const FakePhone &phone)
{
    QStringList commands;
    const QStringList received = phone.receivedCommands();
    for (const QString &command : received) {
        if (command.startsWith(QLatin1StringView("AT+CALA")))
            commands.append(command);
    }
    return commands;
}

} // namespace

class TestAlarms : public QObject
{
    Q_OBJECT

public:
    explicit TestAlarms(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void alarmsOfTheClockAppAreRead();
    void newAlarmIsCreatedAndAnExistingOneChanged();
    void phoneWithoutClockAccessIsNotSupported();
    void invalidAlarmNeverReachesThePhone_data();
    void invalidAlarmNeverReachesThePhone();
    void commandHasTheFormTheClientReads();
    void linesThatCannotBeReadAreSkipped();
    void alarmsNeedAConnection();

private:
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<AlarmService> m_service;
};

void TestAlarms::init()
{
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->config().alarms = QStringList{
        QStringLiteral("\"06:45\",3,0,\"Arbeit\",\"1,2,3,4,5\",1"),
        QStringLiteral("\"09:30\",5,1,\"Arzt=3A Termin\",,0"),
        QStringLiteral("\"08:00\",8,0,\"Jeden Tag\",\"0\",1"),
    };
    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_service = std::make_unique<AlarmService>(m_connection.get());
}

void TestAlarms::cleanup()
{
    m_service.reset();
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
    m_phone.reset();
}

void TestAlarms::alarmsOfTheClockAppAreRead()
{
    const QFuture<Result<QList<PhoneAlarm>>> read = m_service->alarms();
    QVERIFY(waitFor(read, timeoutMs));

    QVERIFY(read.result().hasValue());
    const QList<PhoneAlarm> alarms = read.result().value();
    QCOMPARE(alarms.size(), 3);
    QCOMPARE(alarms.at(0), (PhoneAlarm{3, QTime(6, 45), QStringLiteral("Arbeit"), {1, 2, 3, 4, 5}, true}));
    QCOMPARE(alarms.at(1), (PhoneAlarm{5, QTime(9, 30), QStringLiteral("Arzt: Termin"), {}, false}));
    QCOMPARE(alarms.at(2).days, QList<int>({1, 2, 3, 4, 5, 6, 7}));
}

void TestAlarms::newAlarmIsCreatedAndAnExistingOneChanged()
{
    const PhoneAlarm created{0, QTime(7, 15), QStringLiteral("Wecker, früh"), {6, 7}, true};
    PhoneAlarm changed{3, QTime(6, 30), QStringLiteral("Arbeit"), {1, 2, 3, 4, 5}, false};

    const QFuture<Result<void>> first = m_service->save(created);
    QVERIFY(waitFor(first, timeoutMs));
    const QFuture<Result<void>> second = m_service->save(changed);
    QVERIFY(waitFor(second, timeoutMs));
    const QFuture<Result<QList<PhoneAlarm>>> read = m_service->alarms();
    QVERIFY(waitFor(read, timeoutMs));

    QVERIFY(first.result().hasValue());
    QVERIFY(second.result().hasValue());
    QCOMPARE(alarmCommands(*m_phone).mid(0, 2),
             QStringList({QStringLiteral("AT+CALA=\"07:15\",0,0,\"Wecker=2C früh\",\"6,7\",1"),
                          QStringLiteral("AT+CALA=\"06:30\",3,0,\"Arbeit\",\"1,2,3,4,5\",0")}));
    QVERIFY(read.result().hasValue());
    const QList<PhoneAlarm> alarms = read.result().value();
    QCOMPARE(alarms.size(), 4);
    QCOMPARE(alarms.at(0), changed);
    QCOMPARE(alarms.at(3).name, created.name);
    QCOMPARE(alarms.at(3).days, created.days);
}

void TestAlarms::phoneWithoutClockAccessIsNotSupported()
{
    m_phone->config().alarms.reset();

    const QFuture<Result<QList<PhoneAlarm>>> read = m_service->alarms();
    QVERIFY(waitFor(read, timeoutMs));
    const QFuture<Result<void>> saved =
        m_service->save(PhoneAlarm{0, QTime(7, 0), QStringLiteral("x"), {}, true});
    QVERIFY(waitFor(saved, timeoutMs));

    QVERIFY(!read.result().hasValue());
    QCOMPARE(read.result().error().code(), ErrorCode::NotSupported);
    QCOMPARE(saved.result().error().code(), ErrorCode::NotSupported);
    QCOMPARE(m_connection->state(), PhoneConnection::State::Ready);
}

void TestAlarms::invalidAlarmNeverReachesThePhone_data()
{
    QTest::addColumn<PhoneAlarm>("alarm");

    QTest::newRow("no time") << PhoneAlarm{0, QTime(), QStringLiteral("x"), {}, true};
    QTest::newRow("day 8") << PhoneAlarm{0, QTime(7, 0), QStringLiteral("x"), {1, 8}, true};
    QTest::newRow("day 0") << PhoneAlarm{0, QTime(7, 0), QStringLiteral("x"), {0}, true};
    QTest::newRow("negative id") << PhoneAlarm{-1, QTime(7, 0), QStringLiteral("x"), {}, true};
}

void TestAlarms::invalidAlarmNeverReachesThePhone()
{
    QFETCH(PhoneAlarm, alarm);

    const QFuture<Result<void>> saved = m_service->save(alarm);
    QVERIFY(waitFor(saved, timeoutMs));

    QCOMPARE(saved.result().error().code(), ErrorCode::InvalidInput);
    QVERIFY(alarmCommands(*m_phone).isEmpty());
}

void TestAlarms::commandHasTheFormTheClientReads()
{
    QCOMPARE(AlarmService::saveCommand(PhoneAlarm{0, QTime(7, 5), QStringLiteral("Aufstehen"), {}, true}),
             QStringLiteral("AT+CALA=\"07:05\",0,1,\"Aufstehen\",1"));
    QCOMPARE(AlarmService::saveCommand(
                 PhoneAlarm{12, QTime(22, 0), QStringLiteral("\"Zitat\""), {1, 7}, false}),
             QStringLiteral("AT+CALA=\"22:00\",12,0,\"=22Zitat=22\",\"1,7\",0"));
}

void TestAlarms::linesThatCannotBeReadAreSkipped()
{
    const QList<PhoneAlarm> alarms = AlarmService::parseAlarms(
        {QStringLiteral("+CALA: \"25:99\",1,1,\"x\",,1"), QStringLiteral("+CALA: \"07:00\",x,1,\"y\",,1"),
         QStringLiteral("+CALA: \"07:00\""), QStringLiteral("*ALIVE: 0,72,0"),
         QStringLiteral("+CALA: \"07:00\",4,0,\"z\",\"2,9\",1")});

    QCOMPARE(alarms.size(), 1);
    QCOMPARE(alarms.constFirst().id, 4);
    QCOMPARE(alarms.constFirst().days, QList<int>({2}));
}

void TestAlarms::alarmsNeedAConnection()
{
    m_connection->disconnectFromPhone();

    const QFuture<Result<QList<PhoneAlarm>>> read = m_service->alarms();
    QVERIFY(waitFor(read, timeoutMs));

    QCOMPARE(read.result().error().code(), ErrorCode::ConnectionClosed);
}

QTEST_GUILESS_MAIN(TestAlarms)

#include "tst_alarms.moc"
