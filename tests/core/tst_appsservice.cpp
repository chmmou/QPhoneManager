#include "core/codec/AppListCodec.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/AppsService.h"
#include "core/service/FileService.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::AppIcon;
using qpm::core::service::AppsService;
using qpm::core::service::FileService;
using qpm::test::FakeApp;
using qpm::test::FakePhone;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 20000;

QDateTime installedAt()
{
    return {QDate(2026, 9, 1), QTime(8, 30), QTimeZone::UTC};
}

// The start of a ZIP archive, which an APK is, and some made-up content.
QByteArray apkData(qsizetype size, int seed)
{
    QByteArray data("PK\x03\x04", 4);
    while (data.size() < size)
        data.append(char((data.size() * 13 + seed) & 0xFF));
    return data.left(size);
}

QByteArray iconPng(const QColor &color)
{
    QImage image(48, 48, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QList<FakeApp> exampleApps()
{
    FakeApp notes;
    notes.package = QStringLiteral("org.example.notes");
    notes.label = QStringLiteral("Notizen, schnell");
    notes.version = QStringLiteral("3.2");
    notes.sourcePath = QStringLiteral("/data/app/~~Ab3d==/org.example.notes-Zx9==/base.apk");
    notes.installed = installedAt();
    notes.apk = apkData(300000, 1);
    notes.icon = iconPng(QColor(0, 106, 106));
    FakeApp widget;
    widget.package = QStringLiteral("org.example.widget");
    widget.label = QStringLiteral("Wetter-Widget");
    widget.version = QStringLiteral("1.0");
    widget.sourcePath = QStringLiteral("/data/app/~~Cd5f==/org.example.widget-Yw8==/base.apk");
    widget.installed = installedAt();
    widget.apk = apkData(2000, 2);
    widget.launchable = false;
    FakeApp settings;
    settings.package = QStringLiteral("com.android.settings");
    settings.label = QStringLiteral("Einstellungen");
    settings.version = QStringLiteral("16");
    settings.userApp = false;
    settings.sourcePath = QStringLiteral("/system/priv-app/Settings/Settings.apk");
    settings.installed = installedAt();
    settings.apk = apkData(5000, 3);
    settings.icon = iconPng(QColor(90, 60, 150));
    FakeApp shared;
    shared.package = QStringLiteral("android.ext.shared");
    shared.userApp = false;
    return {notes, widget, settings, shared};
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

} // namespace

// The app service against the fake phone: the list, the icons, starting,
// asking for a removal, saving an APK and handing one to the phone, and the
// reports the phone sends after the user confirmed a change there.
class TestAppsService : public QObject
{
    Q_OBJECT

public:
    explicit TestAppsService(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void listHasEveryAppWithItsFields();
    void refusedListIsAnError();
    void iconsComeInOneSessionAndMissingOnesAreLeftOut();
    void tooManyIconsOrBadNamesAreRefusedBeforeAnythingIsSent();
    void startOpensTheAppOrSaysThatItHasNoWindow();
    void uninstallOnlyAsksThePhone();
    void installationOnThePhoneIsReported();
    void unreadableReportIsIgnored();
    void apkIsSavedFromItsPathOnThePhone();
    void appWithoutApkPathCannotBeSaved();
    void apkGoesToTheRootOfThePhoneForInstallation();
    void installStartsAgainWhenThePhoneMissedTheChannel();
    void withoutTheFastChannelASmallApkGoesInline();
    void onlyAnApkIsInstalled_data();
    void onlyAnApkIsInstalled();
    void invalidPackageNamesAreRefusedBeforeAnythingIsSent();
    void everyCallNeedsAConnection();

private:
    [[nodiscard]] QStringList appCommands() const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<FileService> m_files;
    std::unique_ptr<AppsService> m_service;
};

void TestAppsService::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_phone->setApps(exampleApps());
    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_files = std::make_unique<FileService>(m_connection.get());
    m_service = std::make_unique<AppsService>(m_connection.get(), m_files.get());
    m_phone->clearLog();
}

void TestAppsService::cleanup()
{
    if (m_phone)
        QCOMPARE(m_phone->protocolErrors(), QStringList());
    m_service.reset();
    m_files.reset();
    m_connection.reset();
    m_phone.reset();
    m_dir.reset();
}

QStringList TestAppsService::appCommands() const
{
    QStringList result;
    const QStringList received = m_phone->receivedCommands();
    for (const QString &command : received) {
        if (command.startsWith(QLatin1StringView("AT*EJAVA")) ||
            command.contains(QLatin1StringView("appicon")) ||
            command.contains(QLatin1StringView(".apk"))) {
            result.append(command);
        }
    }
    return result;
}

void TestAppsService::listHasEveryAppWithItsFields()
{
    const QFuture<Result<QList<AppInfo>>> listed = m_service->list();
    QVERIFY(waitFor(listed, timeoutMs));
    QVERIFY2(listed.result().hasValue(), qPrintable(listed.result().error().message()));
    const QList<AppInfo> apps = listed.result().value();

    QCOMPARE(apps.size(), 4);
    QCOMPARE(apps.at(0).package, QStringLiteral("org.example.notes"));
    QCOMPARE(apps.at(0).label, QStringLiteral("Notizen, schnell"));
    QCOMPARE(apps.at(0).version, QStringLiteral("3.2"));
    QVERIFY(apps.at(0).isUserApp);
    QCOMPARE(apps.at(0).sourcePath,
             QStringLiteral("/data/app/~~Ab3d==/org.example.notes-Zx9==/base.apk"));
    QCOMPARE(apps.at(0).installed, installedAt());
    QCOMPARE(apps.at(0).size, std::optional<quint64>(300000));
    QVERIFY(!apps.at(2).isUserApp);
    QCOMPARE(apps.at(3).label, QStringLiteral("android.ext.shared"));
    QVERIFY(apps.at(3).version.isEmpty());
    QVERIFY(apps.at(3).sourcePath.isEmpty());
    QCOMPARE(appCommands(), QStringList{QStringLiteral("AT*EJAVA=1")});
}

void TestAppsService::refusedListIsAnError()
{
    m_phone->config().scriptedReplies.insert(QStringLiteral("AT*EJAVA=1"),
                                             {QByteArrayLiteral("AT*EJAVA=1\r\nERROR\r\n")});

    const QFuture<Result<QList<AppInfo>>> listed = m_service->list();

    QVERIFY(waitFor(listed, timeoutMs));
    QVERIFY(!listed.result().hasValue());
    QCOMPARE(listed.result().error().code(), ErrorCode::Rejected);
}

void TestAppsService::iconsComeInOneSessionAndMissingOnesAreLeftOut()
{
    const QFuture<Result<QList<AppIcon>>> fetched = m_service->fetchIcons(
        {QStringLiteral("org.example.notes"), QStringLiteral("org.example.widget"),
         QStringLiteral("com.android.settings"), QStringLiteral("org.example.unknown")});

    QVERIFY(waitFor(fetched, timeoutMs));
    QVERIFY2(fetched.result().hasValue(), qPrintable(fetched.result().error().message()));
    const QList<AppIcon> icons = fetched.result().value();
    QCOMPARE(icons.size(), 2);
    QCOMPARE(icons.at(0).package, QStringLiteral("org.example.notes"));
    QCOMPARE(icons.at(0).png, exampleApps().at(0).icon);
    QCOMPARE(icons.at(1).package, QStringLiteral("com.android.settings"));
    const QStringList received = m_phone->receivedCommands();
    QCOMPARE(received.count(QStringLiteral("AT+CPROT=0")), 1);
    QVERIFY(received.contains(QStringLiteral("OBEX GET \\appicon\\org.example.notes\\48")));
    QVERIFY(received.contains(QStringLiteral("OBEX GET \\appicon\\org.example.unknown\\48")));
}

void TestAppsService::tooManyIconsOrBadNamesAreRefusedBeforeAnythingIsSent()
{
    QStringList many;
    for (qsizetype i = 0; i <= AppsService::maxIconsPerRequest; ++i)
        many.append(QStringLiteral("org.example.app%1").arg(i));
    const QFuture<Result<QList<AppIcon>>> tooMany = m_service->fetchIcons(many);
    QVERIFY(waitFor(tooMany, timeoutMs));
    QCOMPARE(tooMany.result().error().code(), ErrorCode::LimitExceeded);

    const QFuture<Result<QList<AppIcon>>> bad =
        m_service->fetchIcons({QStringLiteral("org.example.notes\\..\\x")});
    QVERIFY(waitFor(bad, timeoutMs));
    QCOMPARE(bad.result().error().code(), ErrorCode::InvalidInput);
    QVERIFY(m_phone->receivedCommands().isEmpty());
}

void TestAppsService::startOpensTheAppOrSaysThatItHasNoWindow()
{
    const QFuture<Result<void>> started = m_service->start(QStringLiteral("org.example.notes"));
    QVERIFY(waitFor(started, timeoutMs));
    QVERIFY(started.result().hasValue());

    const QFuture<Result<void>> refused = m_service->start(QStringLiteral("org.example.widget"));
    QVERIFY(waitFor(refused, timeoutMs));
    QVERIFY(!refused.result().hasValue());
    QCOMPARE(refused.result().error().code(), ErrorCode::Rejected);

    QCOMPARE(m_phone->startRequests(), (QStringList{QStringLiteral("org.example.notes"),
                                                    QStringLiteral("org.example.widget")}));
    QCOMPARE(appCommands(), (QStringList{QStringLiteral("AT*EJAVA=4,org.example.notes"),
                                         QStringLiteral("AT*EJAVA=4,org.example.widget")}));
}

// The phone answers at once and asks its user; nothing is removed before the
// user confirms it there.
void TestAppsService::uninstallOnlyAsksThePhone()
{
    QSignalSpy removed(m_service.get(), &AppsService::appRemoved);

    const QFuture<Result<void>> asked =
        m_service->requestUninstall(QStringLiteral("org.example.notes"));
    QVERIFY(waitFor(asked, timeoutMs));
    QVERIFY(asked.result().hasValue());
    QCOMPARE(m_phone->uninstallRequests(), QStringList{QStringLiteral("org.example.notes")});
    QCOMPARE(m_phone->apps().size(), 4);
    QCOMPARE(removed.count(), 0);

    m_phone->confirmUninstall(QStringLiteral("org.example.notes"));

    QTRY_COMPARE_WITH_TIMEOUT(removed.count(), 1, timeoutMs);
    QCOMPARE(removed.first().first().toString(), QStringLiteral("org.example.notes"));
}

void TestAppsService::installationOnThePhoneIsReported()
{
    QSignalSpy added(m_service.get(), &AppsService::appAdded);
    FakeApp game;
    game.package = QStringLiteral("org.example.game");
    game.label = QStringLiteral("Würfel");
    game.version = QStringLiteral("0.9 \"beta\"");
    game.sourcePath = QStringLiteral("/data/app/~~Ef==/org.example.game-Gh==/base.apk");
    game.installed = installedAt();
    game.apk = apkData(1234, 4);

    m_phone->confirmInstall(game);

    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, timeoutMs);
    const auto app = added.first().first().value<AppInfo>();
    QCOMPARE(app.package, game.package);
    QCOMPARE(app.label, game.label);
    QCOMPARE(app.version, game.version);
    QCOMPARE(app.size, std::optional<quint64>(1234));
}

void TestAppsService::unreadableReportIsIgnored()
{
    QSignalSpy added(m_service.get(), &AppsService::appAdded);
    QSignalSpy removed(m_service.get(), &AppsService::appRemoved);

    m_phone->sendUnsolicited("*EJAVA: \"\",\"\",\"\",0,org example");
    m_phone->sendUnsolicited("*EJAVA: \"A\",\"\",\"1\",1,org.example.ok");

    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, timeoutMs);
    QCOMPARE(removed.count(), 0);
}

void TestAppsService::apkIsSavedFromItsPathOnThePhone()
{
    const QFuture<Result<QList<AppInfo>>> listed = m_service->list();
    QVERIFY(waitFor(listed, timeoutMs));
    const AppInfo notes = listed.result().value().at(0);
    QSignalSpy progress(m_files.get(), &FileService::transferProgress);
    const QString target = m_dir->filePath(QStringLiteral("Notizen.apk"));

    const QFuture<Result<void>> saved = m_service->backup(notes, target);

    QVERIFY(waitFor(saved, timeoutMs));
    QVERIFY2(saved.result().hasValue(), qPrintable(saved.result().error().message()));
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), exampleApps().at(0).apk);
    QCOMPARE(QFileInfo(target).lastModified().toUTC(), installedAt());
    QVERIFY(!progress.isEmpty());
    QCOMPARE(progress.last().at(0).toULongLong(), 300000ULL);
    const QStringList received = m_phone->receivedCommands();
    QVERIFY(received.contains(
        QStringLiteral("OBEX SETPATH \\phone\\data\\app\\~~Ab3d==\\org.example.notes-Zx9==")));
    QVERIFY(received.contains(QStringLiteral("OBEX SOCKETGET base.apk")));
}

void TestAppsService::appWithoutApkPathCannotBeSaved()
{
    AppInfo shared;
    shared.package = QStringLiteral("android.ext.shared");

    const QFuture<Result<void>> saved =
        m_service->backup(shared, m_dir->filePath(QStringLiteral("x.apk")));

    QVERIFY(waitFor(saved, timeoutMs));
    QCOMPARE(saved.result().error().code(), ErrorCode::NotSupported);
    QVERIFY(m_phone->receivedCommands().isEmpty());
}

// The phone takes an APK at the root of its file browser as the request to
// install it; it is not stored as a file there.
void TestAppsService::apkGoesToTheRootOfThePhoneForInstallation()
{
    const QString source = m_dir->filePath(QStringLiteral("Würfel-0.9.apk"));
    const QByteArray data = apkData(250000, 5);
    QVERIFY(writeFile(source, data));
    const QStringList filesBefore = m_phone->filePaths();

    const QFuture<Result<void>> sent = m_service->install(source);

    QVERIFY(waitFor(sent, timeoutMs));
    QVERIFY2(sent.result().hasValue(), qPrintable(sent.result().error().message()));
    const auto requests = m_phone->installRequests();
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests.first().first, QStringLiteral("Würfel-0.9.apk"));
    QCOMPARE(requests.first().second, data);
    QCOMPARE(m_phone->filePaths(), filesBefore);
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("OBEX SOCKETPUT Würfel-0.9.apk")));
}

// The rule for the fast channel holds for an APK as for any upload: what the
// phone did not receive is sent again.
void TestAppsService::installStartsAgainWhenThePhoneMissedTheChannel()
{
    m_phone->config().missedUploadChannels = 1;
    const QString source = m_dir->filePath(QStringLiteral("app.apk"));
    const QByteArray data = apkData(80000, 6);
    QVERIFY(writeFile(source, data));

    const QFuture<Result<void>> sent = m_service->install(source);

    QVERIFY(waitFor(sent, timeoutMs));
    QVERIFY2(sent.result().hasValue(), qPrintable(sent.result().error().message()));
    const auto requests = m_phone->installRequests();
    QVERIFY(!requests.isEmpty());
    QCOMPARE(requests.last().second, data);
    QCOMPARE(m_phone->receivedCommands().count(QStringLiteral("OBEX SOCKETPUT app.apk")), 2);
}

void TestAppsService::withoutTheFastChannelASmallApkGoesInline()
{
    m_files->setEndpointResolver([](quint16) { return std::nullopt; });
    const QString source = m_dir->filePath(QStringLiteral("small.apk"));
    const QByteArray data = apkData(30000, 7);
    QVERIFY(writeFile(source, data));

    const QFuture<Result<void>> sent = m_service->install(source);

    QVERIFY(waitFor(sent, timeoutMs));
    QVERIFY2(sent.result().hasValue(), qPrintable(sent.result().error().message()));
    QCOMPARE(m_phone->installRequests().size(), 1);
    QCOMPARE(m_phone->installRequests().constFirst().second, data);
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("OBEX PUT small.apk")));
}

void TestAppsService::onlyAnApkIsInstalled_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<int>("code");

    QTest::newRow("other suffix") << QStringLiteral("app.zip") << apkData(100, 1)
                                  << int(ErrorCode::InvalidInput);
    QTest::newRow("no ZIP content")
        << QStringLiteral("fake.apk") << QByteArray(100, 'x') << int(ErrorCode::InvalidInput);
    QTest::newRow("empty") << QStringLiteral("empty.apk") << QByteArray()
                           << int(ErrorCode::InvalidInput);
    QTest::newRow("missing") << QString() << QByteArray() << int(ErrorCode::IoFailure);
}

void TestAppsService::onlyAnApkIsInstalled()
{
    QFETCH(QString, name);
    QFETCH(QByteArray, data);
    QFETCH(int, code);
    QString source = m_dir->filePath(QStringLiteral("missing.apk"));
    if (!name.isEmpty()) {
        source = m_dir->filePath(name);
        QVERIFY(writeFile(source, data));
    }

    const QFuture<Result<void>> sent = m_service->install(source);

    QVERIFY(waitFor(sent, timeoutMs));
    QVERIFY(!sent.result().hasValue());
    QCOMPARE(int(sent.result().error().code()), code);
    QVERIFY(m_phone->receivedCommands().isEmpty());
    QVERIFY(m_phone->installRequests().isEmpty());
}

void TestAppsService::invalidPackageNamesAreRefusedBeforeAnythingIsSent()
{
    const QString hostile = QStringLiteral("org.example\r\nATD0800;");
    const QFuture<Result<void>> started = m_service->start(hostile);
    const QFuture<Result<void>> asked = m_service->requestUninstall(QStringLiteral("a,b"));
    AppInfo app;
    app.package = QStringLiteral("x y");
    app.sourcePath = QStringLiteral("/data/app/a/base.apk");
    app.size = 10;
    const QFuture<Result<void>> saved =
        m_service->backup(app, m_dir->filePath(QStringLiteral("a")));

    for (const QFuture<Result<void>> &future : {started, asked, saved}) {
        QVERIFY(waitFor(future, timeoutMs));
        QCOMPARE(future.result().error().code(), ErrorCode::InvalidInput);
    }
    QVERIFY(m_phone->receivedCommands().isEmpty());
}

void TestAppsService::everyCallNeedsAConnection()
{
    m_connection->disconnectFromPhone();
    QTRY_COMPARE(m_connection->state(), PhoneConnection::State::Disconnected);
    const QString source = m_dir->filePath(QStringLiteral("app.apk"));
    QVERIFY(writeFile(source, apkData(100, 1)));
    AppInfo notes;
    notes.package = QStringLiteral("org.example.notes");
    notes.sourcePath = QStringLiteral("/data/app/a/base.apk");
    notes.size = 10;

    const QFuture<Result<QList<AppInfo>>> listed = m_service->list();
    const QFuture<Result<QList<AppIcon>>> icons =
        m_service->fetchIcons({QStringLiteral("org.example.notes")});
    const QFuture<Result<void>> started = m_service->start(notes.package);
    const QFuture<Result<void>> asked = m_service->requestUninstall(notes.package);
    const QFuture<Result<void>> saved =
        m_service->backup(notes, m_dir->filePath(QStringLiteral("b.apk")));
    const QFuture<Result<void>> sent = m_service->install(source);

    QVERIFY(waitFor(listed, timeoutMs));
    QCOMPARE(listed.result().error().code(), ErrorCode::ConnectionClosed);
    QVERIFY(waitFor(icons, timeoutMs));
    QCOMPARE(icons.result().error().code(), ErrorCode::ConnectionClosed);
    for (const QFuture<Result<void>> &future : {started, asked, saved, sent}) {
        QVERIFY(waitFor(future, timeoutMs));
        QCOMPARE(future.result().error().code(), ErrorCode::ConnectionClosed);
    }
}

QTEST_MAIN(TestAppsService)

#include "tst_appsservice.moc"
