#include "core/service/DeviceStatusService.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::protocol;
using namespace qpm::core::service;
using qpm::test::FakePhone;
using qpm::test::waitFor;

namespace {
constexpr int connectTimeoutMs = 10000;
}

class TestDeviceStatus : public QObject
{
    Q_OBJECT

public:
    explicit TestDeviceStatus(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void operatorIsReadFromTheSimReply_data();
    void operatorIsReadFromTheSimReply();
    void simCardsAreListedWithTheirDefaults();
    void malformedSimLinesAreSkipped();
    void storageRootsAreReadAndCheckedForSense_data();
    void storageRootsAreReadAndCheckedForSense();
    void displaySizeIsRead_data();
    void displaySizeIsRead();

    void overviewIsFilledAfterTheHandshake();
    void missingSimPermissionIsReported();
    void storageIsAskedForAgainAfterObex();
    void overviewIsClearedWhenTheConnectionEnds();

private:
    [[nodiscard]] bool connectToPhone();

    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<DeviceStatusService> m_service;
};

void TestDeviceStatus::init()
{
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_connection = std::make_unique<PhoneConnection>();
    m_service = std::make_unique<DeviceStatusService>(m_connection.get());
}

void TestDeviceStatus::cleanup()
{
    m_service.reset();
    m_connection.reset();
    m_phone.reset();
}

bool TestDeviceStatus::connectToPhone()
{
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    return QTest::qWaitFor(
        [this]() {
            return m_service->overview().display.has_value() &&
                   !m_service->overview().firmware.isEmpty() &&
                   !m_service->overview().storage.isEmpty();
        },
        connectTimeoutMs);
}

void TestDeviceStatus::operatorIsReadFromTheSimReply_data()
{
    QTest::addColumn<QStringList>("lines");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("country");

    QTest::newRow("operator") << QStringList{QStringLiteral("*SOPS: Example Net,26201,DE")}
                              << QStringLiteral("Example Net") << QStringLiteral("de");
    QTest::newRow("encoded comma")
        << QStringList{QStringLiteral("*SOPS: Netz=2C mobil,26202,de")}
        << QStringLiteral("Netz, mobil") << QStringLiteral("de");
    QTest::newRow("umlaut") << QStringList{QStringLiteral("*SOPS: Fünf Netz,26203,de")}
                            << QStringLiteral("Fünf Netz") << QStringLiteral("de");
    QTest::newRow("no SIM") << QStringList{QStringLiteral("*SOPS: ,,")} << QString() << QString();
    QTest::newRow("other line") << QStringList{QStringLiteral("+CSQ: 1,99")} << QString()
                                << QString();
}

void TestDeviceStatus::operatorIsReadFromTheSimReply()
{
    QFETCH(QStringList, lines);
    QFETCH(QString, name);
    QFETCH(QString, country);

    const std::optional<NetworkOperator> parsed = DeviceStatusService::parseOperator(lines);

    QCOMPARE(parsed.has_value(), !name.isEmpty());
    if (parsed) {
        QCOMPARE(parsed->name, name);
        QCOMPARE(parsed->country, country);
    }
}

void TestDeviceStatus::simCardsAreListedWithTheirDefaults()
{
    const QList<SimCard> sims = DeviceStatusService::parseSims(
        {QStringLiteral("*SIM: 0,+491550000009,Work,Example Net,VS"),
         QStringLiteral("*SIM: 1,,Private=3A home,Other Net,")});

    QCOMPARE(sims.size(), 2);
    QCOMPARE(sims.at(0).slot, 0);
    QCOMPARE(sims.at(0).name, QStringLiteral("Work"));
    QVERIFY(sims.at(0).defaultForCalls);
    QVERIFY(sims.at(0).defaultForMessages);
    QCOMPARE(sims.at(1).name, QStringLiteral("Private: home"));
    QCOMPARE(sims.at(1).carrier, QStringLiteral("Other Net"));
    QVERIFY(!sims.at(1).defaultForCalls);
    QVERIFY(sims.at(1).number.isEmpty());
}

void TestDeviceStatus::malformedSimLinesAreSkipped()
{
    QStringList lines = {QStringLiteral("*SIM: x,1,a,b,V"), QStringLiteral("*SIM: 99,1,a,b,V"),
                         QStringLiteral("*SIM: 0,1")};
    for (int i = 0; i < 20; ++i)
        lines.append(QStringLiteral("*SIM: 1,,Name,Carrier,"));

    const QList<SimCard> sims = DeviceStatusService::parseSims(lines);

    QCOMPARE(sims.size(), 8);
    QCOMPARE(sims.first().slot, 1);
}

void TestDeviceStatus::storageRootsAreReadAndCheckedForSense_data()
{
    QTest::addColumn<QStringList>("lines");
    QTest::addColumn<QStringList>("roots");

    QTest::newRow("all roots") << QStringList{QStringLiteral("/sdcard:64000,32000"),
                                              QStringLiteral("/external:128000,1000"),
                                              QStringLiteral("/data:64000,32000")}
                               << QStringList{QStringLiteral("sdcard"), QStringLiteral("external"),
                                              QStringLiteral("data")};
    QTest::newRow("free above total") << QStringList{QStringLiteral("/sdcard:100,200")}
                                      << QStringList();
    QTest::newRow("zero total") << QStringList{QStringLiteral("/sdcard:0,0")} << QStringList();
    QTest::newRow("unknown root") << QStringList{QStringLiteral("/system:100,50")}
                                  << QStringList();
    QTest::newRow("not a number") << QStringList{QStringLiteral("/data:abc,1")} << QStringList();
    QTest::newRow("negative") << QStringList{QStringLiteral("/data:-100,1")} << QStringList();
}

void TestDeviceStatus::storageRootsAreReadAndCheckedForSense()
{
    QFETCH(QStringList, lines);
    QFETCH(QStringList, roots);

    const QList<StorageSpace> storage = DeviceStatusService::parseStorage(lines);

    QStringList found;
    for (const StorageSpace &space : storage)
        found.append(space.root);
    QCOMPARE(found, roots);
    if (!storage.isEmpty()) {
        QCOMPARE(storage.first().totalBytes, 64000ULL);
        QCOMPARE(storage.first().freeBytes, 32000ULL);
    }
}

void TestDeviceStatus::displaySizeIsRead_data()
{
    QTest::addColumn<QString>("line");
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("dpi");

    QTest::newRow("full") << QStringLiteral("*DISPLAY: 1080,2400,409.432,411.891,420") << 1080
                          << 420;
    QTest::newRow("no density") << QStringLiteral("*DISPLAY: 720,1280") << 720 << 0;
    QTest::newRow("zero width") << QStringLiteral("*DISPLAY: 0,1280,1,1,1") << 0 << 0;
    QTest::newRow("garbage") << QStringLiteral("*DISPLAY: wide") << 0 << 0;
}

void TestDeviceStatus::displaySizeIsRead()
{
    QFETCH(QString, line);
    QFETCH(int, width);
    QFETCH(int, dpi);

    const std::optional<DisplayInfo> display = DeviceStatusService::parseDisplay({line});

    QCOMPARE(display.has_value(), width > 0);
    if (display) {
        QCOMPARE(display->width, width);
        QCOMPARE(display->densityDpi, dpi);
    }
}

void TestDeviceStatus::overviewIsFilledAfterTheHandshake()
{
    m_phone->config().scriptedReplies.insert(
        QStringLiteral("AT*SOPS"), {QByteArrayLiteral("AT*SOPS\r\n*SOPS: Example Net,26201,de\r\nOK\r\n")});
    m_phone->config().scriptedReplies.insert(
        QStringLiteral("AT*SIM"),
        {QByteArrayLiteral("AT*SIM\r\n*SIM: 0,,Work,Example Net,VS\r\nOK\r\n")});

    QVERIFY(connectToPhone());
    QTRY_VERIFY(m_service->overview().status.cpuPercent.has_value());

    const DeviceOverview &overview = m_service->overview();
    QVERIFY(m_service->hasData());
    QCOMPARE(overview.identity.model, m_phone->config().model);
    QCOMPARE(overview.identity.androidVersion, QStringLiteral("Android 14"));
    QCOMPARE(overview.status.batteryPercent, std::optional<int>(72));
    QCOMPARE(overview.status.temperatureCelsius, std::optional<int>(24));
    QCOMPARE(overview.status.signal, std::optional<int>(17));
    QCOMPARE(overview.status.cpuPercent, std::optional<int>(12));
    QCOMPARE(overview.networkOperator->name, QStringLiteral("Example Net"));
    QCOMPARE(overview.sims.size(), 1);
    QVERIFY(overview.simsKnown);
    QCOMPARE(overview.storage.size(), 2);
    QCOMPARE(overview.display->height, 2400);
    QCOMPARE(overview.firmware, QStringLiteral("EX1A.260101.001"));
}

void TestDeviceStatus::missingSimPermissionIsReported()
{
    m_phone->config().scriptedReplies.insert(
        QStringLiteral("AT*SIM"), {QByteArrayLiteral("AT*SIM\r\n*SIM: PERMISSION DENIED\r\nERROR\r\n")});

    QVERIFY(connectToPhone());
    QTRY_VERIFY(m_service->overview().simPermissionMissing);

    QVERIFY(!m_service->overview().simsKnown);
    QVERIFY(m_service->overview().sims.isEmpty());
}

void TestDeviceStatus::storageIsAskedForAgainAfterObex()
{
    QVERIFY(connectToPhone());
    const qsizetype before = m_phone->receivedCommands().count(QStringLiteral("AT*STATFS"));
    QSignalSpy changed(m_service.get(), &DeviceStatusService::changed);

    const QFuture<Result<int>> work = m_connection->withObex<int>(
        ObexClient::Target::IrmcSync,
        [](ObexClient &) { return QtFuture::makeReadyValueFuture(Result<int>(1)); });
    QVERIFY(waitFor(work));

    QTRY_COMPARE(m_phone->receivedCommands().count(QStringLiteral("AT*STATFS")), before + 1);
    QTRY_VERIFY(!changed.isEmpty());
    QCOMPARE(m_service->overview().storage.size(), 2);
}

void TestDeviceStatus::overviewIsClearedWhenTheConnectionEnds()
{
    QVERIFY(connectToPhone());
    QSignalSpy changed(m_service.get(), &DeviceStatusService::changed);

    m_connection->disconnectFromPhone();

    QVERIFY(!m_service->hasData());
    QVERIFY(m_service->overview().storage.isEmpty());
    QVERIFY(!m_service->overview().status.batteryPercent.has_value());
    QCOMPARE(changed.count(), 1);
}

QTEST_GUILESS_MAIN(TestDeviceStatus)

#include "tst_devicestatus.moc"
