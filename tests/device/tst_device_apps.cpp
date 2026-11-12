#include "CommandGuard.h"

#include "core/codec/AppListCodec.h"
#include "core/protocol/AdbBridge.h"
#include "core/protocol/PhoneConnection.h"
#include "core/protocol/TcpTransport.h"
#include "core/service/AppsService.h"
#include "core/service/FileService.h"

#include "WaitFor.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

using namespace qpm::core;
using namespace qpm::core::codec;
using namespace qpm::core::protocol;
using qpm::core::service::AppIcon;
using qpm::core::service::AppsService;
using qpm::core::service::FileService;
using qpm::test::CommandGuard;
using qpm::test::waitFor;

namespace {

using State = PhoneConnection::State;
constexpr quint16 clientPort = 5210;
constexpr int readyTimeoutMs = 30000;
constexpr int stepTimeoutMs = 60000;
constexpr int phoneUiTimeoutMs = 60000;
constexpr int connectTries = 10;
constexpr quint64 maxBackupBytes = 64ULL * 1024 * 1024;

QString clientPackage()
{
    return QStringLiteral("com.fjsoft.myphoneexplorer.client");
}

QString settingsPackage()
{
    return QStringLiteral("com.android.settings");
}

// The only package the test installs and removes; nothing else is touched.
QString testPackage()
{
    return QStringLiteral("org.qphonemanager.devicetest");
}

QByteArray sha256Of(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
        return {};
    return hash.result();
}

std::optional<AppInfo> appNamed(const QList<AppInfo> &apps, const QString &package)
{
    const auto found = std::find_if(apps.cbegin(), apps.cend(), [&package](const AppInfo &app) {
        return app.package == package;
    });
    if (found == apps.cend())
        return std::nullopt;
    return *found;
}

QList<QByteArray> trimmedLines(const QByteArray &text)
{
    QList<QByteArray> lines = text.split('\n');
    for (QByteArray &line : lines)
        line = line.trimmed();
    return lines;
}

bool hasSignalWith(const QSignalSpy &spy, const std::function<bool(const QVariant &)> &matches)
{
    for (qsizetype i = 0; i < spy.count(); ++i) {
        if (matches(spy.at(i).constFirst()))
            return true;
    }
    return false;
}

// The newest directory below `folder` by its version name, or nothing.
QString newestEntry(const QString &folder)
{
    QStringList entries = QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(entries.begin(), entries.end(), [](const QString &a, const QString &b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
    return entries.isEmpty() ? QString() : QDir(folder).filePath(entries.constLast());
}

} // namespace

// Runs against a real client over adb, through the guard against sending and
// calling. Everywhere the apps are counted and checked for their form, and
// the icon of the client itself is read. On the emulator an app is opened, the
// APK of a preinstalled app is saved and compared with the file on the
// phone, and a test app built with the Android SDK is installed and removed
// again, confirmed through adb. On any other phone, which may be someone's
// own, nothing is opened, saved, installed or removed, and only counts are
// printed, never the name of an app.
class TestDeviceApps : public QObject
{
    Q_OBJECT

public:
    explicit TestDeviceApps(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void appsAreListedAndWellFormed();
    void iconOfTheClientArrives();
    void appOpensOnTheEmulator();
    void apkOfAPreinstalledAppIsSavedOnTheEmulator();
    void removalRequestIsAnsweredOnTheEmulator();
    void ownApkIsInstalledAndRemovedOnTheEmulator();

private:
    [[nodiscard]] bool connectToClient();
    [[nodiscard]] Result<QList<AppInfo>> listApps();
    /// Runs adb with `arguments` for the device under test; nothing on a failure.
    [[nodiscard]] std::optional<QByteArray> adb(const QStringList &arguments,
                                                int timeoutMs = readyTimeoutMs) const;
    [[nodiscard]] bool isInstalled(const QString &package) const;
    [[nodiscard]] bool resumedActivityIs(const QString &package) const;
    /**
     * Taps the first clickable element on the emulator's screen whose text
     * is one of `texts`, until `done` holds or the time is up; for the
     * questions the phone asks its user.
     */
    [[nodiscard]] bool tapUntil(const QStringList &texts, const std::function<bool()> &done);
    /// Builds and signs a test APK; the reason why not otherwise.
    [[nodiscard]] Result<QString> buildTestApk(const QString &folder) const;
    void removeTestPackage();

    QString m_adbPath;
    QString m_serial;
    bool m_emulator = false;
    std::unique_ptr<AdbBridge> m_adb;
    quint16 m_localPort = 0;
    quint16 m_filePort = 0;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<FileService> m_files;
    std::unique_ptr<AppsService> m_apps;
    QPointer<CommandGuard> m_guard;
};

void TestDeviceApps::initTestCase()
{
    m_adbPath = qEnvironmentVariable("QPM_TEST_ADB");
    m_serial = qEnvironmentVariable("QPM_TEST_SERIAL");
    if (m_adbPath.isEmpty() || m_serial.isEmpty())
        QSKIP("QPM_TEST_ADB and QPM_TEST_SERIAL are not set");
    QVERIFY2(QDir::isAbsolutePath(m_adbPath), "QPM_TEST_ADB must be an absolute path");
    m_emulator = m_serial.startsWith(QStringLiteral("emulator-"));
    qInfo() << (m_emulator ? "Target: emulator, apps are opened, saved, installed and removed"
                           : "Target: phone, apps are only counted");

    m_adb = std::make_unique<AdbBridge>(m_adbPath);
    const QFuture<Result<void>> server = m_adb->startServer();
    QVERIFY(waitFor(server, readyTimeoutMs));
    QVERIFY2(server.result().hasValue(), "adb start-server failed");
    // The error text of adb can name the device, so only the code is shown.
    for (const auto &[phonePort, local] : {std::pair<quint16, quint16 *>{clientPort, &m_localPort},
                                           {FileService::fileChannelPort, &m_filePort}}) {
        const QFuture<Result<quint16>> forward = m_adb->forward(m_serial, phonePort);
        QVERIFY(waitFor(forward, readyTimeoutMs));
        QVERIFY2(forward.result().hasValue(),
                 qPrintable(QStringLiteral("adb forward failed with code %1")
                                .arg(int(forward.result().error().code()))));
        *local = forward.result().value();
    }
    const QFuture<Result<void>> client = m_adb->startClient(m_serial);
    QVERIFY(waitFor(client, readyTimeoutMs));
    QVERIFY2(client.result().hasValue(),
             qPrintable(QStringLiteral("Starting the client failed with code %1")
                            .arg(int(client.result().error().code()))));
    if (m_emulator)
        removeTestPackage();
}

void TestDeviceApps::cleanupTestCase()
{
    if (!m_adb)
        return;
    if (m_emulator)
        removeTestPackage();
    for (const quint16 local : {m_localPort, m_filePort}) {
        if (local == 0)
            continue;
        const QFuture<Result<void>> removed = m_adb->removeForward(m_serial, local);
        QVERIFY(waitFor(removed, readyTimeoutMs));
    }
}

void TestDeviceApps::init()
{
    m_connection = std::make_unique<PhoneConnection>();
    QVERIFY(connectToClient());
    m_files = std::make_unique<FileService>(m_connection.get());
    // The guard hides the transport, so the side channel is named here.
    m_files->setEndpointResolver([this](quint16 phonePort) -> std::optional<Endpoint> {
        if (phonePort == FileService::fileChannelPort)
            return Endpoint{QStringLiteral("127.0.0.1"), m_filePort};
        return std::nullopt;
    });
    m_apps = std::make_unique<AppsService>(m_connection.get(), m_files.get());
}

void TestDeviceApps::cleanup()
{
    if (m_guard)
        QVERIFY2(m_guard->blocked().isEmpty(), "The guard refused a command");
    m_apps.reset();
    m_files.reset();
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
}

bool TestDeviceApps::connectToClient()
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

Result<QList<AppInfo>> TestDeviceApps::listApps()
{
    const QFuture<Result<QList<AppInfo>>> listed = m_apps->list();
    if (!waitFor(listed, stepTimeoutMs))
        return Error(ErrorCode::Timeout, QStringLiteral("the app list did not finish"));
    return listed.result();
}

std::optional<QByteArray> TestDeviceApps::adb(const QStringList &arguments, int timeoutMs) const
{
    QProcess process;
    process.start(m_adbPath, QStringList{QStringLiteral("-s"), m_serial} + arguments);
    if (!process.waitForFinished(timeoutMs) || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        return std::nullopt;
    }
    return process.readAllStandardOutput();
}

bool TestDeviceApps::isInstalled(const QString &package) const
{
    const std::optional<QByteArray> listed =
        adb({QStringLiteral("shell"), QStringLiteral("pm"), QStringLiteral("list"),
             QStringLiteral("packages"), package});
    if (!listed)
        return false;
    const QList<QByteArray> lines = listed->split('\n');
    return std::any_of(lines.cbegin(), lines.cend(), [&package](const QByteArray &line) {
        return line.trimmed() == "package:" + package.toUtf8();
    });
}

bool TestDeviceApps::resumedActivityIs(const QString &package) const
{
    const std::optional<QByteArray> dump =
        adb({QStringLiteral("shell"), QStringLiteral("dumpsys"), QStringLiteral("activity"),
             QStringLiteral("activities")});
    if (!dump)
        return false;
    const QList<QByteArray> lines = dump->split('\n');
    return std::any_of(lines.cbegin(), lines.cend(), [&package](const QByteArray &line) {
        return line.contains("topResumedActivity") && line.contains(package.toUtf8() + '/');
    });
}

bool TestDeviceApps::tapUntil(const QStringList &texts, const std::function<bool()> &done)
{
    static const QRegularExpression node(QStringLiteral("<node [^>]*>"));
    static const QRegularExpression textAttribute(QStringLiteral("text=\"([^\"]*)\""));
    static const QRegularExpression bounds(
        QStringLiteral("bounds=\"\\[(\\d+),(\\d+)\\]\\[(\\d+),(\\d+)\\]\""));
    return QTest::qWaitFor(
        [&]() {
            if (done())
                return true;
            const QString dumpPath = QStringLiteral("/sdcard/qpm-ui.xml");
            const std::optional<QByteArray> dumped =
                adb({QStringLiteral("shell"), QStringLiteral("uiautomator"), QStringLiteral("dump"),
                     dumpPath});
            const std::optional<QByteArray> xml =
                dumped ? adb({QStringLiteral("shell"), QStringLiteral("cat"), dumpPath})
                       : std::nullopt;
            if (!xml)
                return false;
            const QString text = QString::fromUtf8(*xml);
            auto it = node.globalMatch(text);
            while (it.hasNext()) {
                const QString element = it.next().captured(0);
                const QString label = textAttribute.match(element).captured(1);
                const QRegularExpressionMatch box = bounds.match(element);
                if (!texts.contains(label, Qt::CaseInsensitive) ||
                    !element.contains(QLatin1StringView("clickable=\"true\"")) || !box.hasMatch()) {
                    continue;
                }
                const int x = (box.captured(1).toInt() + box.captured(3).toInt()) / 2;
                const int y = (box.captured(2).toInt() + box.captured(4).toInt()) / 2;
                static_cast<void>(
                    adb({QStringLiteral("shell"), QStringLiteral("input"), QStringLiteral("tap"),
                         QString::number(x), QString::number(y)}));
                break;
            }
            return done();
        },
        phoneUiTimeoutMs);
}

Result<QString> TestDeviceApps::buildTestApk(const QString &folder) const
{
    const QString sdk = QFileInfo(QFileInfo(m_adbPath).absolutePath()).absolutePath();
    const QString buildTools = newestEntry(QDir(sdk).filePath(QStringLiteral("build-tools")));
    const QString platform = newestEntry(QDir(sdk).filePath(QStringLiteral("platforms")));
    const QString aapt2 = QDir(buildTools).filePath(QStringLiteral("aapt2"));
    const QString zipalign = QDir(buildTools).filePath(QStringLiteral("zipalign"));
    const QString apksigner = QDir(buildTools).filePath(QStringLiteral("apksigner"));
    const QString androidJar = QDir(platform).filePath(QStringLiteral("android.jar"));
    const QString keytool = QStandardPaths::findExecutable(QStringLiteral("keytool"));
    for (const QString &tool : {aapt2, zipalign, apksigner, androidJar}) {
        if (buildTools.isEmpty() || platform.isEmpty() || !QFileInfo::exists(tool)) {
            return Error(
                ErrorCode::NotFound,
                QStringLiteral("the Android SDK at %1 has no build tools or no platform").arg(sdk));
        }
    }
    if (keytool.isEmpty())
        return Error(ErrorCode::NotFound, QStringLiteral("keytool is not installed"));

    const QDir dir(folder);
    QFile manifest(dir.filePath(QStringLiteral("AndroidManifest.xml")));
    if (!manifest.open(QIODevice::WriteOnly))
        return Error(ErrorCode::IoFailure, QStringLiteral("the manifest cannot be written"));
    manifest.write("<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\" "
                   "package=\"" +
                   testPackage().toUtf8() +
                   "\" android:versionCode=\"1\" android:versionName=\"1.0\">\n"
                   "<uses-sdk android:minSdkVersion=\"24\" android:targetSdkVersion=\"34\"/>\n"
                   "<application android:label=\"QPM Testapp\" android:hasCode=\"false\"/>\n"
                   "</manifest>\n");
    manifest.close();
    const QString unsignedApk = dir.filePath(QStringLiteral("unsigned.apk"));
    const QString alignedApk = dir.filePath(QStringLiteral("aligned.apk"));
    const QString signedApk = dir.filePath(QStringLiteral("qpm-testapp.apk"));
    const QString keystore = dir.filePath(QStringLiteral("test.jks"));
    const QString password = QStringLiteral("qpm-test-only");
    const QList<std::pair<QString, QStringList>> steps = {
        {aapt2,
         {QStringLiteral("link"), QStringLiteral("-o"), unsignedApk, QStringLiteral("--manifest"),
          manifest.fileName(), QStringLiteral("-I"), androidJar}},
        {zipalign, {QStringLiteral("-f"), QStringLiteral("4"), unsignedApk, alignedApk}},
        {keytool,
         {QStringLiteral("-genkeypair"), QStringLiteral("-keystore"), keystore,
          QStringLiteral("-storepass"), password, QStringLiteral("-keypass"), password,
          QStringLiteral("-alias"), QStringLiteral("qpm"), QStringLiteral("-keyalg"),
          QStringLiteral("RSA"), QStringLiteral("-keysize"), QStringLiteral("2048"),
          QStringLiteral("-validity"), QStringLiteral("2"), QStringLiteral("-dname"),
          QStringLiteral("CN=QPM Test"), QStringLiteral("-noprompt")}},
        {apksigner,
         {QStringLiteral("sign"), QStringLiteral("--ks"), keystore, QStringLiteral("--ks-pass"),
          QStringLiteral("pass:") + password, QStringLiteral("--out"), signedApk, alignedApk}},
    };
    for (const auto &[program, arguments] : steps) {
        QProcess process;
        process.start(program, arguments);
        if (!process.waitForFinished(stepTimeoutMs) || process.exitCode() != 0) {
            return Error(ErrorCode::IoFailure,
                         QStringLiteral("%1 failed").arg(QFileInfo(program).fileName()));
        }
    }
    return signedApk;
}

void TestDeviceApps::removeTestPackage()
{
    if (isInstalled(testPackage()))
        static_cast<void>(adb({QStringLiteral("uninstall"), testPackage()}));
}

void TestDeviceApps::appsAreListedAndWellFormed()
{
    const Result<QList<AppInfo>> listed = listApps();
    QVERIFY2(listed.hasValue(), qPrintable(listed.error().message()));
    const QList<AppInfo> apps = listed.value();
    int user = 0;
    int withApk = 0;
    for (const AppInfo &app : apps) {
        QVERIFY(isValidPackageName(app.package));
        QVERIFY(!app.label.isEmpty());
        if (app.isUserApp)
            ++user;
        if (!app.sourcePath.isEmpty()) {
            ++withApk;
            QVERIFY(apkPhonePath(app.sourcePath).hasValue());
            QVERIFY(app.installed.isValid());
            QVERIFY(app.size.has_value());
        }
    }
    // Counts only; the names of the apps stay on the phone.
    qInfo() << "Apps:" << apps.size() << "of which the user's own:" << user
            << "with an APK path:" << withApk;
    QVERIFY(apps.size() > 10);
    QVERIFY(user >= 1);
    QVERIFY(appNamed(apps, clientPackage()).has_value());
}

void TestDeviceApps::iconOfTheClientArrives()
{
    const QFuture<Result<QList<AppIcon>>> fetched = m_apps->fetchIcons({clientPackage()});
    QVERIFY(waitFor(fetched, stepTimeoutMs));
    QVERIFY2(fetched.result().hasValue(), qPrintable(fetched.result().error().message()));
    QCOMPARE(fetched.result().value().size(), 1);
    const QImage icon = QImage::fromData(fetched.result().value().constFirst().png, "PNG");
    QVERIFY(!icon.isNull());
    QCOMPARE(icon.size(), QSize(AppsService::iconSize, AppsService::iconSize));
    qInfo() << "Icon" << icon.size() << fetched.result().value().constFirst().png.size() << "bytes";
}

void TestDeviceApps::appOpensOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("Apps are opened on the emulator only");
    const Result<QList<AppInfo>> listed = listApps();
    QVERIFY(listed.hasValue());
    QVERIFY(appNamed(listed.value(), settingsPackage()).has_value());

    const QFuture<Result<void>> started = m_apps->start(settingsPackage());
    QVERIFY(waitFor(started, stepTimeoutMs));
    QVERIFY2(started.result().hasValue(), qPrintable(started.result().error().message()));
    QVERIFY(
        QTest::qWaitFor([this]() { return resumedActivityIs(settingsPackage()); }, readyTimeoutMs));
    static_cast<void>(adb({QStringLiteral("shell"), QStringLiteral("input"),
                           QStringLiteral("keyevent"), QStringLiteral("KEYCODE_HOME")}));
}

void TestDeviceApps::apkOfAPreinstalledAppIsSavedOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("APKs are saved on the emulator only");
    const Result<QList<AppInfo>> listed = listApps();
    QVERIFY(listed.hasValue());
    // The smallest preinstalled APK keeps the copy short.
    std::optional<AppInfo> chosen;
    for (const AppInfo &app : listed.value()) {
        if (app.isUserApp || app.sourcePath.isEmpty() || !app.size || *app.size == 0 ||
            *app.size > maxBackupBytes) {
            continue;
        }
        if (!chosen || *app.size < *chosen->size)
            chosen = app;
    }
    QVERIFY2(chosen, "The emulator lists no preinstalled app with an APK");
    QTemporaryDir local;
    QVERIFY(local.isValid());
    const QString target = local.filePath(QStringLiteral("backup.apk"));

    const QFuture<Result<void>> saved = m_apps->backup(*chosen, target);

    QVERIFY(waitFor(saved, stepTimeoutMs));
    QVERIFY2(saved.result().hasValue(), qPrintable(saved.result().error().message()));
    QCOMPARE(quint64(QFileInfo(target).size()), *chosen->size);
    const std::optional<QByteArray> path = adb(
        {QStringLiteral("shell"), QStringLiteral("pm"), QStringLiteral("path"), chosen->package});
    QVERIFY(path);
    QVERIFY(trimmedLines(*path).contains("package:" + chosen->sourcePath.toUtf8()));
    const std::optional<QByteArray> size =
        adb({QStringLiteral("shell"), QStringLiteral("stat"), QStringLiteral("-c"),
             QStringLiteral("%s"), chosen->sourcePath});
    QVERIFY(size);
    QCOMPARE(size->trimmed().toULongLong(), *chosen->size);
    const std::optional<QByteArray> hash =
        adb({QStringLiteral("shell"), QStringLiteral("sha256sum"), chosen->sourcePath});
    QVERIFY(hash);
    QCOMPARE(hash->left(64), sha256Of(target).toHex());
    qInfo() << "Preinstalled APK of" << *chosen->size << "bytes saved byte for byte";
}

// Without an APK of its own the test does not confirm a removal: the request
// for a package that is not installed is answered and changes nothing.
void TestDeviceApps::removalRequestIsAnsweredOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("Removals are asked for on the emulator only");
    QVERIFY(!isInstalled(testPackage()));
    const Result<QList<AppInfo>> before = listApps();
    QVERIFY(before.hasValue());

    const QFuture<Result<void>> asked = m_apps->requestUninstall(testPackage());

    QVERIFY(waitFor(asked, stepTimeoutMs));
    QVERIFY2(asked.result().hasValue(), qPrintable(asked.result().error().message()));
    static_cast<void>(adb({QStringLiteral("shell"), QStringLiteral("input"),
                           QStringLiteral("keyevent"), QStringLiteral("KEYCODE_BACK")}));
    const Result<QList<AppInfo>> after = listApps();
    QVERIFY(after.hasValue());
    QCOMPARE(after.value().size(), before.value().size());
}

void TestDeviceApps::ownApkIsInstalledAndRemovedOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("Apps are installed and removed on the emulator only");
    QTemporaryDir build;
    QVERIFY(build.isValid());
    const Result<QString> apk = buildTestApk(build.path());
    if (!apk.hasValue()) {
        QSKIP(qPrintable(QStringLiteral("No test APK can be built, %1; install and removal are "
                                        "not tried")
                             .arg(apk.error().message())));
    }
    // The client may install apps on the emulator, which a user grants once.
    static_cast<void>(adb({QStringLiteral("shell"), QStringLiteral("appops"), QStringLiteral("set"),
                           clientPackage(), QStringLiteral("REQUEST_INSTALL_PACKAGES"),
                           QStringLiteral("allow")}));
    QSignalSpy added(m_apps.get(), &AppsService::appAdded);
    QSignalSpy removed(m_apps.get(), &AppsService::appRemoved);

    const QFuture<Result<void>> sent = m_apps->install(apk.value());
    QVERIFY(waitFor(sent, stepTimeoutMs));
    QVERIFY2(sent.result().hasValue(), qPrintable(sent.result().error().message()));
    QVERIFY2(tapUntil({QStringLiteral("Install"), QStringLiteral("Update"),
                       QStringLiteral("Install anyway"), QStringLiteral("Done")},
                      [this]() { return isInstalled(testPackage()); }),
             "The phone did not install the test app");
    QTRY_VERIFY_WITH_TIMEOUT(hasSignalWith(added,
                                           [](const QVariant &app) {
                                               return app.value<AppInfo>().package == testPackage();
                                           }),
                             readyTimeoutMs);
    const Result<QList<AppInfo>> listed = listApps();
    QVERIFY(listed.hasValue());
    const std::optional<AppInfo> installed = appNamed(listed.value(), testPackage());
    QVERIFY(installed);
    QCOMPARE(installed->label, QStringLiteral("QPM Testapp"));
    QCOMPARE(installed->version, QStringLiteral("1.0"));
    QVERIFY(installed->isUserApp);
    static_cast<void>(adb({QStringLiteral("shell"), QStringLiteral("input"),
                           QStringLiteral("keyevent"), QStringLiteral("KEYCODE_HOME")}));

    const QFuture<Result<void>> asked = m_apps->requestUninstall(testPackage());
    QVERIFY(waitFor(asked, stepTimeoutMs));
    QVERIFY2(asked.result().hasValue(), qPrintable(asked.result().error().message()));
    if (!tapUntil({QStringLiteral("OK"), QStringLiteral("Uninstall")},
                  [this]() { return !isInstalled(testPackage()); })) {
        qInfo() << "The question of the phone was not found; the test app is removed through adb";
        removeTestPackage();
    }
    QTRY_VERIFY_WITH_TIMEOUT(
        hasSignalWith(removed,
                      [](const QVariant &package) { return package.toString() == testPackage(); }),
        readyTimeoutMs);
    const Result<QList<AppInfo>> left = listApps();
    QVERIFY(left.hasValue());
    QVERIFY(!appNamed(left.value(), testPackage()));
}

QTEST_MAIN(TestDeviceApps)

#include "tst_device_apps.moc"
