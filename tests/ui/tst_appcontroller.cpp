#include "ui/AppController.h"

#include "core/protocol/PhoneConnection.h"
#include "core/store/ProfileStore.h"
#include "platform/SecretStore.h"
#include "ui/DeviceStatusModel.h"
#include "ui/FoundPhoneModel.h"
#include "ui/ProfileListModel.h"
#include "ui/QrCodeProvider.h"
#include "ui/Settings.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace qpm::core;
using qpm::core::store::DeviceProfile;
using qpm::core::store::ProfileStore;
using qpm::test::FakePhone;
using qpm::ui::AppController;
using qpm::ui::DeviceStatusModel;
using qpm::ui::FoundPhoneModel;
using qpm::ui::ProfileListModel;
using qpm::ui::QrCodeProvider;
using qpm::ui::Settings;

namespace {

constexpr int connectTimeoutMs = 10000;
// The connection waits five seconds before it tries again.
constexpr int reconnectTimeoutMs = 15000;

QString row(const QAbstractItemModel *model, int index, const QByteArray &role)
{
    return model->data(model->index(index, 0), model->roleNames().key(role)).toString();
}

} // namespace

class TestAppController : public QObject
{
    Q_OBJECT

public:
    explicit TestAppController(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void connectingOverWifiStoresTheProfileAndFillsTheOverview();
    void addressThatIsNoIpAddressIsRefused_data();
    void addressThatIsNoIpAddressIsRefused();
    void pinIsAskedForAndAWrongOneIsReported();
    void phoneBusyWithAnotherComputerIsExplained();
    void errorMessagesStayShortAndSayWhatToDo_data();
    void errorMessagesStayShortAndSayWhatToDo();
    void connectionOrderFallsBackToTheNextWay();
    void startConnectsToThePhoneOfTheLastSession();
    void lostConnectionIsRestoredWhenAutoConnectIsOn();
    void usbSearchListsTheAdbDevices();
    void missingAdbIsExplained();
    void profileCanBeRenamedAndRemoved();
    void invalidNameIsRefused();
    void qrCodeCarriesSessionAndAddressAndListensOnlyWhileShown();
    void ownAddressPrefersAPrivateIpv4Address_data();
    void ownAddressPrefersAPrivateIpv4Address();
    void syncNeedsAConnectionAndAlwaysCoversTheContacts();
    void syncReportsTheChangesOfEachArea();
    void signalValueMapsToBars_data();
    void signalValueMapsToBars();

private:
    [[nodiscard]] std::unique_ptr<AppController> makeController(
        const AppController::Environment &environment = quietEnvironment());
    [[nodiscard]] static AppController::Environment quietEnvironment();
    [[nodiscard]] QString phoneAddress() const;
    [[nodiscard]] QString writeScript(const QByteArray &body) const;
    void storeProfile(const DeviceProfile &profile) const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<FakePhone> m_phone;
};

void TestAppController::initTestCase()
{
    // Numbers and sizes in the expected texts are written without grouping.
    QLocale::setDefault(QLocale::c());
}

void TestAppController::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_settings = std::make_unique<Settings>(m_dir->filePath(QStringLiteral("settings.ini")));
    m_settings->setDataDirectory(m_dir->filePath(QStringLiteral("profiles")));
    // No adb from the test machine: every test that needs one brings a script.
    m_settings->setAdbPath(m_dir->filePath(QStringLiteral("no-adb")));
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
}

void TestAppController::cleanup()
{
    m_phone.reset();
    m_settings.reset();
    m_dir.reset();
}

AppController::Environment TestAppController::quietEnvironment()
{
    AppController::Environment environment;
    environment.listenPort = 0;
    environment.listenAddress = QHostAddress(QHostAddress::LocalHost);
    environment.searchNetwork = false;
    environment.searchBluetooth = false;
    return environment;
}

std::unique_ptr<AppController>
TestAppController::makeController(const AppController::Environment &environment)
{
    return std::make_unique<AppController>(m_settings.get(), nullptr, environment);
}

QString TestAppController::phoneAddress() const
{
    return QStringLiteral("127.0.0.1:%1").arg(m_phone->port());
}

QString TestAppController::writeScript(const QByteArray &body) const
{
    const QString path = m_dir->filePath(QStringLiteral("adb"));
    QFile script(path);
    if (!script.open(QIODevice::WriteOnly))
        return {};
    script.write("#!/bin/sh\n" + body);
    script.close();
    script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

void TestAppController::storeProfile(const DeviceProfile &profile) const
{
    ProfileStore store(m_settings->dataDirectory(), profile.deviceId);
    QVERIFY(store.open().hasValue());
    QVERIFY(store.saveProfile(profile).hasValue());
}

void TestAppController::connectingOverWifiStoresTheProfileAndFillsTheOverview()
{
    const std::unique_ptr<AppController> controller = makeController();
    QSignalSpy errors(controller.get(), &AppController::errorOccurred);

    QVERIFY(controller->connectAddress(phoneAddress()));
    QVERIFY(controller->isConnecting());
    QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), connectTimeoutMs);

    const DeviceStatusModel *status = controller->status();
    QTRY_VERIFY(!status->display().isEmpty() && status->cpuPercent() >= 0);
    QCOMPARE(controller->transportLabel(), QStringLiteral("Wi-Fi"));
    QCOMPARE(controller->currentDevice(), m_phone->config().deviceId);
    QCOMPARE(controller->currentDeviceName(), m_phone->config().model);
    QCOMPARE(controller->devices()->count(), 1);
    QCOMPARE(m_settings->lastDevice(), m_phone->config().deviceId);
    QVERIFY(status->hasData());
    QCOMPARE(status->deviceName(), m_phone->config().model);
    QCOMPARE(status->androidVersion(), QStringLiteral("Android 14 (Upside Down Cake)"));
    QCOMPARE(status->batteryPercent(), 72);
    QCOMPARE(status->batteryTemperature(), QStringLiteral("24 °C"));
    QCOMPARE(status->batteryVoltage(), QStringLiteral("3.92 V"));
    QCOMPARE(status->signalText(), QStringLiteral("Good"));
    QCOMPARE(status->cpuPercent(), 12);
    QCOMPARE(status->internalStorageUsed(), 0.5);
    QCOMPARE(status->internalStorage(), QStringLiteral("32.0 GB of 64.0 GB free"));
    QCOMPARE(status->cardStorageUsed(), -1.0);
    QCOMPARE(status->display(), QStringLiteral("1080 x 2400 pixels, 420 dpi"));
    QCOMPARE(status->firmware(), QStringLiteral("EX1A.260101.001"));
    QVERIFY(errors.isEmpty());

    ProfileStore store(m_settings->dataDirectory(), m_phone->config().deviceId);
    QVERIFY(store.open().hasValue());
    const Result<DeviceProfile> profile = store.loadProfile();
    QVERIFY(profile.hasValue());
    QCOMPARE(profile.value().fixedAddress, phoneAddress());
    QCOMPARE(profile.value().transport, qpm::core::store::TransportKind::Tcp);

    controller->disconnectPhone();
    QCOMPARE(controller->connectionState(), AppController::ConnectionState::Disconnected);
    QVERIFY(!status->hasData());
    QVERIFY(controller->transportLabel().isEmpty());
}

void TestAppController::addressThatIsNoIpAddressIsRefused_data()
{
    QTest::addColumn<QString>("address");

    QTest::newRow("empty") << QString();
    QTest::newRow("host name") << QStringLiteral("phone.example");
    QTest::newRow("bad port") << QStringLiteral("192.168.1.20:99999");
    QTest::newRow("path") << QStringLiteral("192.168.1.20:5210/x");
    QTest::newRow("user") << QStringLiteral("me@192.168.1.20:5210");
}

void TestAppController::addressThatIsNoIpAddressIsRefused()
{
    QFETCH(QString, address);
    const std::unique_ptr<AppController> controller = makeController();
    QSignalSpy errors(controller.get(), &AppController::errorOccurred);

    QVERIFY(!controller->connectAddress(address));

    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.first().first().toString().contains(QStringLiteral("IP address")));
    QCOMPARE(controller->connectionState(), AppController::ConnectionState::Disconnected);
}

void TestAppController::pinIsAskedForAndAWrongOneIsReported()
{
    m_phone->config().pin = QStringLiteral("1234");
    const std::unique_ptr<AppController> controller = makeController();
    QSignalSpy pins(controller.get(), &AppController::pinRequested);

    QVERIFY(controller->connectAddress(phoneAddress()));
    QVERIFY(pins.wait(connectTimeoutMs));
    QCOMPARE(pins.last().at(0).toInt(), 10);
    QVERIFY(!pins.last().at(1).toBool());
    QCOMPARE(controller->connectionState(), AppController::ConnectionState::WaitingForPin);

    controller->submitPin(QStringLiteral("0000"), false);
    QVERIFY(pins.wait(connectTimeoutMs));
    QCOMPARE(pins.last().at(0).toInt(), 9);
    QVERIFY(pins.last().at(1).toBool());

    controller->submitPin(QStringLiteral("1234"), true);
    QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), connectTimeoutMs);
    QCOMPARE(pins.count(), 2);
    QVERIFY(!controller->canRememberPin());
}

void TestAppController::phoneBusyWithAnotherComputerIsExplained()
{
    m_phone->config().rejectAsAlreadyConnected = true;
    const std::unique_ptr<AppController> controller = makeController();
    QSignalSpy errors(controller.get(), &AppController::errorOccurred);

    QVERIFY(controller->connectAddress(phoneAddress()));

    QVERIFY(errors.wait(connectTimeoutMs));
    QCOMPARE(errors.first().first().toString(),
             AppController::messageFor(Error(ErrorCode::AlreadyConnected, {}),
                                       AppController::ConnectionKind::Wifi));
    QVERIFY(!controller->isConnected());
}

void TestAppController::errorMessagesStayShortAndSayWhatToDo_data()
{
    QTest::addColumn<int>("code");
    QTest::addColumn<int>("kind");

    const QList<ErrorCode> codes = {
        ErrorCode::Timeout,          ErrorCode::ConnectionClosed, ErrorCode::ProtocolError,
        ErrorCode::AccessDenied,     ErrorCode::WrongPin,         ErrorCode::PermissionDenied,
        ErrorCode::NotSupported,     ErrorCode::NotFound,         ErrorCode::Rejected,
        ErrorCode::AlreadyConnected, ErrorCode::WifiDisabled,     ErrorCode::IoFailure,
        ErrorCode::InvalidInput,     ErrorCode::LimitExceeded,
    };
    const QList<AppController::ConnectionKind> kinds = {
        AppController::ConnectionKind::None, AppController::ConnectionKind::Wifi,
        AppController::ConnectionKind::Usb, AppController::ConnectionKind::Bluetooth};
    for (const ErrorCode code : codes) {
        for (const AppController::ConnectionKind kind : kinds) {
            QTest::addRow("code %d, kind %d", int(code), int(kind)) << int(code) << int(kind);
        }
    }
}

// Every message names the problem and the next step: at least two
// sentences, none longer than 25 words, and no protocol words.
void TestAppController::errorMessagesStayShortAndSayWhatToDo()
{
    QFETCH(int, code);
    QFETCH(int, kind);

    const QString message = AppController::messageFor(
        Error(ErrorCode(code), QStringLiteral("technical detail")),
        AppController::ConnectionKind(kind));

    static const QRegularExpression sentenceEnd(QStringLiteral("(?<=\\.)\\s+"));
    const QStringList sentences = message.split(sentenceEnd, Qt::SkipEmptyParts);
    QVERIFY2(sentences.size() >= 2, qPrintable(message));
    for (const QString &sentence : sentences) {
        QVERIFY2(sentence.split(u' ', Qt::SkipEmptyParts).size() <= 25, qPrintable(sentence));
    }
    for (const QString &word : {QStringLiteral("OBEX"), QStringLiteral("AT"),
                                QStringLiteral("technical"), QStringLiteral("TCP")}) {
        QVERIFY2(!message.split(u' ').contains(word), qPrintable(message));
    }
    QVERIFY(AppController::messageFor(Error(ErrorCode::Cancelled, {}),
                                      AppController::ConnectionKind(kind))
                .isEmpty());
}

void TestAppController::connectionOrderFallsBackToTheNextWay()
{
    m_settings->setAdbPath(writeScript("echo \"error: no devices found\" >&2\nexit 1\n"));
    m_settings->setConnectionOrder({QStringLiteral("usb"), QStringLiteral("wifi")});
    DeviceProfile profile;
    profile.deviceId = m_phone->config().deviceId;
    profile.displayName = QStringLiteral("Test phone");
    profile.adbSerial = QStringLiteral("ABC123");
    profile.fixedAddress = phoneAddress();
    storeProfile(profile);
    const std::unique_ptr<AppController> controller = makeController();
    controller->start();
    QSignalSpy errors(controller.get(), &AppController::errorOccurred);

    QVERIFY(controller->connectDevice(profile.deviceId));

    QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), connectTimeoutMs);
    QCOMPARE(controller->transportLabel(), QStringLiteral("Wi-Fi"));
    QVERIFY(errors.isEmpty());
    QCOMPARE(controller->currentDeviceName(), QStringLiteral("Test phone"));
}

void TestAppController::startConnectsToThePhoneOfTheLastSession()
{
    DeviceProfile profile;
    profile.deviceId = m_phone->config().deviceId;
    profile.displayName = QStringLiteral("Test phone");
    profile.fixedAddress = phoneAddress();
    profile.lastConnection = QDateTime::currentDateTimeUtc();
    storeProfile(profile);
    m_settings->setLastDevice(profile.deviceId);
    const std::unique_ptr<AppController> controller = makeController();

    controller->start();

    QCOMPARE(controller->currentDevice(), profile.deviceId);
    QVERIFY(!controller->lastConnectionText().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), connectTimeoutMs);
    QVERIFY(!controller->isListening());
}

void TestAppController::lostConnectionIsRestoredWhenAutoConnectIsOn()
{
    const std::unique_ptr<AppController> controller = makeController();
    QSignalSpy errors(controller.get(), &AppController::errorOccurred);
    QVERIFY(controller->connectAddress(phoneAddress()));
    QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), connectTimeoutMs);

    m_phone->dropConnection();

    QTRY_COMPARE_WITH_TIMEOUT(controller->connectionState(), AppController::ConnectionState::Lost,
                              connectTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), reconnectTimeoutMs);
    QCOMPARE(errors.count(), 1);
    QVERIFY(errors.first().first().toString().contains(QStringLiteral("lost")));
}

void TestAppController::usbSearchListsTheAdbDevices()
{
    m_settings->setAdbPath(writeScript(
        "if [ \"$1\" = devices ]; then\n"
        "printf 'List of devices attached\\n'\n"
        "printf 'ABC123 device product:x model:Pixel_8 device:y transport_id:1\\n'\n"
        "printf 'DEF456 unauthorized transport_id:2\\n'\n"
        "fi\n"));
    const std::unique_ptr<AppController> controller = makeController();

    controller->startSearch();
    QTRY_VERIFY_WITH_TIMEOUT(!controller->isSearching(), connectTimeoutMs);

    const FoundPhoneModel *usb = controller->usbPhones();
    QCOMPARE(usb->count(), 2);
    QCOMPARE(row(usb, 0, "label"), QStringLiteral("Pixel 8"));
    QCOMPARE(row(usb, 0, "address"), QStringLiteral("ABC123"));
    QVERIFY(usb->data(usb->index(0), FoundPhoneModel::AvailableRole).toBool());
    QVERIFY(!usb->data(usb->index(1), FoundPhoneModel::AvailableRole).toBool());
    QVERIFY(row(usb, 1, "detail").contains(QStringLiteral("USB debugging")));
    QVERIFY(controller->usbHint().isEmpty());
}

void TestAppController::missingAdbIsExplained()
{
    const std::unique_ptr<AppController> controller = makeController();
    QSignalSpy errors(controller.get(), &AppController::errorOccurred);

    controller->startSearch();
    QVERIFY(!controller->connectUsb(QStringLiteral("ABC123")));

    QVERIFY(controller->usbHint().contains(QStringLiteral("adb")));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!controller->isSearching());
}

void TestAppController::profileCanBeRenamedAndRemoved()
{
    const std::unique_ptr<AppController> controller = makeController();
    QVERIFY(controller->connectAddress(phoneAddress()));
    QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), connectTimeoutMs);
    const QString id = controller->currentDevice();
    const QString folder =
        QDir(m_settings->dataDirectory()).filePath(ProfileStore::directoryName(id));
    QVERIFY(QFileInfo::exists(folder));

    QVERIFY(controller->renameDevice(id, QStringLiteral("  Work phone ")));
    QCOMPARE(controller->currentDeviceName(), QStringLiteral("Work phone"));
    QCOMPARE(row(controller->devices(), 0, "displayName"), QStringLiteral("Work phone"));

    QVERIFY(controller->removeDevice(id));

    QVERIFY(!controller->isConnected());
    QVERIFY(controller->currentDevice().isEmpty());
    QCOMPARE(controller->devices()->count(), 0);
    QVERIFY(!QFileInfo::exists(folder));
    QVERIFY(m_settings->lastDevice().isEmpty());
}

void TestAppController::invalidNameIsRefused()
{
    DeviceProfile profile;
    profile.deviceId = QStringLiteral("dev-1");
    profile.displayName = QStringLiteral("Old");
    storeProfile(profile);
    const std::unique_ptr<AppController> controller = makeController();
    controller->start();
    QSignalSpy errors(controller.get(), &AppController::errorOccurred);

    QVERIFY(!controller->renameDevice(profile.deviceId, QStringLiteral("   ")));
    QVERIFY(!controller->renameDevice(profile.deviceId, QString(65, u'x')));

    QCOMPARE(errors.count(), 2);
    QCOMPARE(row(controller->devices(), 0, "displayName"), QStringLiteral("Old"));
}

void TestAppController::qrCodeCarriesSessionAndAddressAndListensOnlyWhileShown()
{
    const std::unique_ptr<AppController> controller = makeController();
    QrCodeProvider *qr = controller->qrCode();
    QVERIFY(!controller->isListening());

    qr->setActive(true);

    QVERIFY(controller->isListening());
    QCOMPARE(controller->connectionState(), AppController::ConnectionState::Waiting);
    if (!qr->ownAddress().isEmpty()) {
        static const QRegularExpression form(QStringLiteral(
            "^https://www\\.fjsoft\\.at/mpeclient\\.php\\?sid=[A-Za-z0-9]{20}&ownip=(.+)$"));
        const QRegularExpressionMatch match = form.match(qr->text());
        QVERIFY2(match.hasMatch(), qPrintable(qr->text()));
        QCOMPARE(match.captured(1), qr->ownAddress());
    }

    m_phone->dialOut(QHostAddress(QHostAddress::LocalHost),
                     controller->connection()->listeningPort());
    QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), connectTimeoutMs);
    QCOMPARE(controller->transportLabel(), QStringLiteral("Wi-Fi"));

    qr->setActive(false);
    QVERIFY(!controller->isListening());
    QVERIFY(controller->isConnected());
}

void TestAppController::ownAddressPrefersAPrivateIpv4Address_data()
{
    QTest::addColumn<QStringList>("candidates");
    QTest::addColumn<QString>("chosen");

    QTest::newRow("private after public")
        << QStringList{QStringLiteral("203.0.113.5"), QStringLiteral("192.168.1.10")}
        << QStringLiteral("192.168.1.10");
    QTest::newRow("loopback and link-local skipped")
        << QStringList{QStringLiteral("127.0.0.1"), QStringLiteral("169.254.3.4"),
                       QStringLiteral("10.1.2.3")}
        << QStringLiteral("10.1.2.3");
    QTest::newRow("public only") << QStringList{QStringLiteral("203.0.113.5")}
                                 << QStringLiteral("203.0.113.5");
    QTest::newRow("IPv6 only") << QStringList{QStringLiteral("fd00::1")} << QString();
    QTest::newRow("none") << QStringList() << QString();
}

void TestAppController::ownAddressPrefersAPrivateIpv4Address()
{
    QFETCH(QStringList, candidates);
    QFETCH(QString, chosen);
    QList<QHostAddress> addresses;
    for (const QString &candidate : std::as_const(candidates))
        addresses.append(QHostAddress(candidate));

    const QHostAddress address = QrCodeProvider::chooseOwnAddress(addresses);

    QCOMPARE(address.isNull() ? QString() : address.toString(), chosen);
    QVERIFY(QrCodeProvider::newSessionId() != QrCodeProvider::newSessionId());
}

void TestAppController::syncNeedsAConnectionAndAlwaysCoversTheContacts()
{
    const std::unique_ptr<AppController> controller = makeController();
    QSignalSpy errors(controller.get(), &AppController::errorOccurred);
    QSignalSpy finished(controller.get(), &AppController::syncFinished);

    controller->syncAll();
    QCOMPARE(errors.count(), 1);

    QVERIFY(controller->connectAddress(phoneAddress()));
    QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), connectTimeoutMs);
    controller->syncAll();

    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, connectTimeoutMs);
    QCOMPARE(finished.first().first().toString(),
             QStringLiteral("Contacts: 0 new, 0 changed, 0 deleted"));
}

void TestAppController::syncReportsTheChangesOfEachArea()
{
    const std::unique_ptr<AppController> controller = makeController();
    controller->registerSyncArea(QStringLiteral("Calendar"), []() {
        return QtFuture::makeReadyValueFuture(
            Result<AppController::SyncChanges>(AppController::SyncChanges{1, 2, 3}));
    });
    controller->registerSyncArea(QStringLiteral("Notes"), []() {
        return QtFuture::makeReadyValueFuture(Result<AppController::SyncChanges>(
            Error(ErrorCode::PermissionDenied, QStringLiteral("no access"))));
    });
    QSignalSpy finished(controller.get(), &AppController::syncFinished);
    QVERIFY(controller->connectAddress(phoneAddress()));
    QTRY_VERIFY_WITH_TIMEOUT(controller->isConnected(), connectTimeoutMs);

    controller->syncAll();

    QTRY_COMPARE(finished.count(), 1);
    const QStringList lines = finished.first().first().toString().split(u'\n');
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0), QStringLiteral("Contacts: 0 new, 0 changed, 0 deleted"));
    QCOMPARE(lines.at(1), QStringLiteral("Calendar: 1 new, 2 changed, 3 deleted"));
    QVERIFY(lines.at(2).startsWith(QStringLiteral("Notes: not synced.")));
    QVERIFY(!controller->isSyncing());
}

void TestAppController::signalValueMapsToBars_data()
{
    QTest::addColumn<int>("signal");
    QTest::addColumn<int>("bars");

    QTest::newRow("none") << 0 << 0;
    QTest::newRow("weak") << 5 << 1;
    QTest::newRow("fair") << 12 << 2;
    QTest::newRow("good") << 17 << 3;
    QTest::newRow("very good") << 31 << 4;
    QTest::newRow("unknown") << 99 << -1;
    QTest::newRow("out of range") << 40 << -1;
}

void TestAppController::signalValueMapsToBars()
{
    QFETCH(int, signal);
    QFETCH(int, bars);

    QCOMPARE(DeviceStatusModel::barsForSignal(signal), bars);
}

QTEST_GUILESS_MAIN(TestAppController)

#include "tst_appcontroller.moc"
