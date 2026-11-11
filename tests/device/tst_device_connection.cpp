#include "CommandGuard.h"

#include "core/protocol/AdbBridge.h"
#include "core/protocol/PhoneConnection.h"
#include "core/protocol/TcpTransport.h"
#include "core/service/DeviceStatusService.h"

#include "WaitFor.h"

#include <QDir>
#include <QPointer>
#include <QRegularExpression>
#include <QTest>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::protocol;
using namespace qpm::core::service;
using qpm::test::CommandGuard;
using qpm::test::waitFor;

namespace {

using State = PhoneConnection::State;
constexpr quint16 clientPort = 5210;
constexpr int readyTimeoutMs = 30000;
constexpr int connectTries = 10;

} // namespace

// Runs against a real client over adb, through the guard against sending and
// calling. Every check looks at presence and form only: no value from the
// phone is ever printed, since the phone can be someone's own.
class TestDeviceConnection : public QObject
{
    Q_OBJECT

public:
    explicit TestDeviceConnection(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void handshakeIdentifiesThePhone();
    void overviewValuesArePresentAndWellFormed();
    void disconnectEndsTheSessionAndReconnectRestoresIt();

private:
    [[nodiscard]] bool connectToClient();

    QString m_serial;
    std::unique_ptr<AdbBridge> m_adb;
    quint16 m_localPort = 0;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<DeviceStatusService> m_service;
    QPointer<CommandGuard> m_guard;
};

void TestDeviceConnection::initTestCase()
{
    const QString adbPath = qEnvironmentVariable("QPM_TEST_ADB");
    m_serial = qEnvironmentVariable("QPM_TEST_SERIAL");
    if (adbPath.isEmpty() || m_serial.isEmpty())
        QSKIP("QPM_TEST_ADB and QPM_TEST_SERIAL are not set");
    QVERIFY2(QDir::isAbsolutePath(adbPath), "QPM_TEST_ADB must be an absolute path");

    m_adb = std::make_unique<AdbBridge>(adbPath);
    const QFuture<Result<void>> server = m_adb->startServer();
    QVERIFY(waitFor(server, readyTimeoutMs));
    QVERIFY2(server.result().hasValue(), "adb start-server failed");
    const QFuture<Result<quint16>> forward = m_adb->forward(m_serial, clientPort);
    QVERIFY(waitFor(forward, readyTimeoutMs));
    // The error text of adb can name the device, so only the code is shown.
    QVERIFY2(forward.result().hasValue(),
             qPrintable(QStringLiteral("adb forward failed with code %1")
                            .arg(int(forward.result().error().code()))));
    m_localPort = forward.result().value();
    const QFuture<Result<void>> client = m_adb->startClient(m_serial);
    QVERIFY(waitFor(client, readyTimeoutMs));
    QVERIFY2(client.result().hasValue(),
             qPrintable(QStringLiteral("Starting the client failed with code %1")
                            .arg(int(client.result().error().code()))));
}

void TestDeviceConnection::cleanupTestCase()
{
    if (!m_adb || m_localPort == 0)
        return;
    const QFuture<Result<void>> removed = m_adb->removeForward(m_serial, m_localPort);
    QVERIFY(waitFor(removed, readyTimeoutMs));
}

void TestDeviceConnection::init()
{
    m_connection = std::make_unique<PhoneConnection>();
    m_service = std::make_unique<DeviceStatusService>(m_connection.get());
    QVERIFY(connectToClient());
}

void TestDeviceConnection::cleanup()
{
    if (m_guard)
        QVERIFY2(m_guard->blocked().isEmpty(), "The guard refused a command");
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_service.reset();
    m_connection.reset();
}

// The client may still be starting after "am start", so a refused
// connection is tried again a few times.
bool TestDeviceConnection::connectToClient()
{
    for (int attempt = 0; attempt < connectTries; ++attempt) {
        auto guard = std::make_unique<CommandGuard>(
            std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), m_localPort));
        m_guard = guard.get();
        m_connection->connectTransport(std::move(guard));
        const bool settled = QTest::qWaitFor(
            [this]() {
                return m_connection->state() == State::Ready ||
                       m_connection->state() == State::Lost;
            },
            readyTimeoutMs);
        if (settled && m_connection->state() == State::Ready)
            return true;
        // Only spaces out the tries; no result depends on how long it is.
        QTest::qWait(1000);
    }
    return false;
}

void TestDeviceConnection::handshakeIdentifiesThePhone()
{
    const DeviceIdentity &identity = m_connection->identity();
    static const QRegularExpression versionForm(QStringLiteral("^\\d+\\.\\d+"));

    QVERIFY2(!identity.deviceId.isEmpty() && identity.deviceId.size() <= 128,
             "device id missing or too long");
    QVERIFY2(!identity.model.isEmpty(), "model missing");
    QVERIFY2(identity.androidApi >= 21 && identity.androidApi < 100, "Android API level out of range");
    QVERIFY2(versionForm.match(identity.clientVersion).hasMatch(), "client version malformed");
    QVERIFY2(identity.androidVersion.startsWith(QStringLiteral("Android")),
             "Android version malformed");
    QVERIFY2(identity.smsCapabilities.contains(QStringLiteral("TXT")),
             "message capabilities missing");
}

void TestDeviceConnection::overviewValuesArePresentAndWellFormed()
{
    QTRY_VERIFY2_WITH_TIMEOUT(m_service->overview().display.has_value() &&
                                  !m_service->overview().storage.isEmpty() &&
                                  !m_service->overview().firmware.isEmpty(),
                              "details did not arrive", readyTimeoutMs);
    m_connection->refreshStatus();
    QTRY_VERIFY2_WITH_TIMEOUT(m_service->overview().status.cpuPercent.has_value(),
                              "processor load did not arrive", readyTimeoutMs);

    const DeviceOverview &overview = m_service->overview();
    QVERIFY2(overview.status.batteryPercent && *overview.status.batteryPercent >= 0 &&
                 *overview.status.batteryPercent <= 100,
             "battery level missing or out of range");
    QVERIFY2(overview.status.charging.has_value(), "charging state missing");
    QVERIFY2(!overview.status.signal || (*overview.status.signal >= 0 && *overview.status.signal <= 99),
             "signal out of range");
    QVERIFY2(*overview.status.cpuPercent >= 0 && *overview.status.cpuPercent <= 100,
             "processor load out of range");
    QVERIFY2(overview.display->width > 0 && overview.display->height > 0,
             "display size missing");
    bool sharedStorage = false;
    for (const StorageSpace &space : overview.storage) {
        QVERIFY2(space.totalBytes > 0 && space.freeBytes <= space.totalBytes,
                 "storage values out of range");
        sharedStorage = sharedStorage || space.root == QLatin1StringView("sdcard");
    }
    QVERIFY2(sharedStorage, "internal shared storage missing");
    QVERIFY2(overview.simsKnown || overview.simPermissionMissing, "SIM query unanswered");
    QVERIFY2(overview.sims.size() <= 8, "too many SIM cards");
}

void TestDeviceConnection::disconnectEndsTheSessionAndReconnectRestoresIt()
{
    const QString firstId = m_connection->identity().deviceId;

    m_connection->disconnectFromPhone();

    QCOMPARE(m_connection->state(), State::Disconnected);
    QVERIFY(!m_service->hasData());
    QVERIFY(m_connection->session() == nullptr);

    QVERIFY(connectToClient());
    QVERIFY2(m_connection->identity().deviceId == firstId,
             "the phone reported another device id after reconnecting");
    QTRY_VERIFY_WITH_TIMEOUT(m_service->hasData(), readyTimeoutMs);
}

QTEST_GUILESS_MAIN(TestDeviceConnection)

#include "tst_device_connection.moc"
