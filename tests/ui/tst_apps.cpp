#include "core/protocol/PhoneConnection.h"
#include "core/service/AppsService.h"
#include "core/service/FileService.h"
#include "ui/apps/AppListModel.h"
#include "ui/contacts/PhoneImageProvider.h"

#include "FakePhone.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QLocale>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <memory>

using namespace qpm::core;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::AppsService;
using qpm::core::service::FileService;
using qpm::test::FakeApp;
using qpm::test::FakePhone;
using qpm::ui::AppListModel;
using qpm::ui::PhoneImageProvider;
using qpm::ui::PhotoSource;

namespace {

constexpr int timeoutMs = 20000;

QDateTime installedAt()
{
    return {QDate(2026, 9, 1), QTime(8, 30), QTimeZone::UTC};
}

QByteArray apkData(qsizetype size, int seed)
{
    QByteArray data("PK\x03\x04", 4);
    while (data.size() < size)
        data.append(char((data.size() * 17 + seed) & 0xFF));
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

FakeApp fakeApp(const QString &package, const QString &label, bool user, int seed)
{
    FakeApp app;
    app.package = package;
    app.label = label;
    app.version = QStringLiteral("%1.0").arg(seed);
    app.userApp = user;
    app.sourcePath = user ? QStringLiteral("/data/app/~~x%1==/%2/base.apk").arg(seed).arg(package)
                          : QStringLiteral("/system/app/%1/base.apk").arg(package);
    app.installed = installedAt();
    app.apk = apkData(1000 * seed, seed);
    app.icon = iconPng(QColor(20 * seed, 106, 106));
    return app;
}

QString imagePrefix()
{
    return QStringLiteral("image://qpm/");
}

QString role(const QAbstractItemModel &model, int row, const QByteArray &name)
{
    return model.data(model.index(row, 0), model.roleNames().key(name)).toString();
}

QStringList column(const QAbstractItemModel &model, const QByteArray &name)
{
    QStringList result;
    for (int row = 0; row < model.rowCount(); ++row)
        result.append(role(model, row, name));
    return result;
}

} // namespace

// The apps of the phone for the window: the list with filter and search,
// the icons behind their image address, the actions and what they report.
class TestApps : public QObject
{
    Q_OBJECT

public:
    explicit TestApps(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    void yourAppsAreShownSortedWithTheirRoles();
    void filterAndSearchNarrowTheList();
    void iconsAreHandedOutUnderTheirAddress();
    void iconAddressChangesWithTheIcon();
    void startReportsWhetherTheAppOpened();
    void removalIsAskedAndShownOnceConfirmed();
    void apkIsSavedWithProgress();
    void apkIsSentAndTheInstallationAnnounced();
    void notAnApkIsRefusedWithAText();
    void secondCopyWaitsForTheFirst();
    void refusedListShowsWhatToDo();
    void lostConnectionEmptiesTheList();

private:
    [[nodiscard]] bool loadAndWait();
    [[nodiscard]] QString iconUrl(const QString &package) const;

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<FileService> m_files;
    std::unique_ptr<AppsService> m_service;
    std::shared_ptr<PhotoSource> m_photos;
    std::unique_ptr<AppListModel> m_model;
};

void TestApps::initTestCase()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
}

void TestApps::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    FakeApp widget =
        fakeApp(QStringLiteral("org.example.widget"), QStringLiteral("Wetter"), true, 2);
    widget.launchable = false;
    FakeApp bare;
    bare.package = QStringLiteral("android.ext.shared");
    bare.userApp = false;
    m_phone->setApps(
        {fakeApp(QStringLiteral("org.example.zoo"), QStringLiteral("Zoo"), true, 1), widget,
         fakeApp(QStringLiteral("org.example.uebung"), QStringLiteral("Übungen"), true, 3),
         fakeApp(QStringLiteral("com.android.settings"), QStringLiteral("Einstellungen"), false, 4),
         bare});
    m_connection = std::make_unique<PhoneConnection>();
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_files = std::make_unique<FileService>(m_connection.get());
    m_service = std::make_unique<AppsService>(m_connection.get(), m_files.get());
    m_photos = std::make_shared<PhotoSource>();
    m_model = std::make_unique<AppListModel>(m_service.get(), m_files.get(), m_photos);
    m_model->setConnected(true);
}

void TestApps::cleanup()
{
    m_model.reset();
    m_service.reset();
    m_files.reset();
    m_connection.reset();
    m_phone.reset();
    m_dir.reset();
}

bool TestApps::loadAndWait()
{
    if (!m_model->load())
        return false;
    // The icons come last, in an OBEX session of their own; the phone drops
    // what it reports on its own while one runs.
    return QTest::qWaitFor(
        [this]() {
            return !m_model->isBusy() && m_model->totalCount() > 0 &&
                   !m_photos->appIcon(QStringLiteral("com.android.settings")).isNull();
        },
        timeoutMs);
}

QString TestApps::iconUrl(const QString &package) const
{
    const int row = m_model->rowOf(package);
    return row < 0 ? QString() : m_model->index(row).data(AppListModel::IconUrlRole).toString();
}

void TestApps::yourAppsAreShownSortedWithTheirRoles()
{
    QVERIFY(m_phone->receivedCommands().filter(QStringLiteral("EJAVA")).isEmpty());
    QVERIFY(loadAndWait());

    QCOMPARE(m_model->filter(), AppListModel::Filter::User);
    QCOMPARE(m_model->totalCount(), 5);
    QCOMPARE(
        column(*m_model, "label"),
        (QStringList{QStringLiteral("Übungen"), QStringLiteral("Wetter"), QStringLiteral("Zoo")}));
    const QHash<int, QByteArray> names = m_model->roleNames();
    for (const QByteArray &name :
         {QByteArrayLiteral("packageName"), QByteArrayLiteral("label"),
          QByteArrayLiteral("version"), QByteArrayLiteral("size"), QByteArrayLiteral("iconUrl"),
          QByteArrayLiteral("isUserApp"), QByteArrayLiteral("installed"),
          QByteArrayLiteral("installedText")}) {
        QVERIFY2(std::find(names.cbegin(), names.cend(), name) != names.cend(), name.constData());
    }
    const QModelIndex first = m_model->index(0);
    QCOMPARE(first.data(AppListModel::PackageRole).toString(),
             QStringLiteral("org.example.uebung"));
    QCOMPARE(first.data(AppListModel::VersionRole).toString(), QStringLiteral("3.0"));
    QCOMPARE(first.data(AppListModel::SizeRole).toDouble(), 3000.0);
    QCOMPARE(first.data(AppListModel::InstalledRole).toDateTime(), installedAt());
    QVERIFY(first.data(AppListModel::IsUserAppRole).toBool());
    QVERIFY(first.data(AppListModel::CanBackupRole).toBool());
    QCOMPARE(first.data(AppListModel::InstalledTextRole).toString(),
             QLocale().toString(installedAt().toLocalTime().date(), QLocale::ShortFormat));
    QVERIFY(first.data(AppListModel::DetailsRole)
                .toString()
                .startsWith(QStringLiteral("Version 3.0, ")));
    QCOMPARE(m_model->backupFileName(QStringLiteral("org.example.uebung")),
             QStringLiteral("Übungen-3.0.apk"));
}

void TestApps::filterAndSearchNarrowTheList()
{
    QVERIFY(loadAndWait());
    QSignalSpy counted(m_model.get(), &AppListModel::countChanged);

    m_model->setFilter(AppListModel::Filter::System);
    QCOMPARE(column(*m_model, "packageName"),
             (QStringList{QStringLiteral("android.ext.shared"),
                          QStringLiteral("com.android.settings")}));
    QVERIFY(!m_model->canBackup(QStringLiteral("android.ext.shared")));

    m_model->setFilter(AppListModel::Filter::All);
    QCOMPARE(m_model->count(), 5);

    m_model->setSearchText(QStringLiteral("üBUNG"));
    QCOMPARE(column(*m_model, "packageName"), QStringList{QStringLiteral("org.example.uebung")});
    m_model->setSearchText(QStringLiteral("android"));
    QCOMPARE(m_model->count(), 2);
    m_model->setSearchText(QStringLiteral("nothing like it"));
    QCOMPARE(m_model->count(), 0);
    QCOMPARE(m_model->emptyText(), QStringLiteral("No app matches the search or filter."));
    QVERIFY(counted.count() >= 5);
}

void TestApps::iconsAreHandedOutUnderTheirAddress()
{
    QVERIFY(loadAndWait());
    QTRY_VERIFY_WITH_TIMEOUT(!iconUrl(QStringLiteral("org.example.zoo")).isEmpty(), timeoutMs);
    const QString url = iconUrl(QStringLiteral("org.example.zoo"));
    QVERIFY2(url.startsWith(imagePrefix() + QStringLiteral("app/org.example.zoo/")),
             qPrintable(url));

    PhoneImageProvider provider(m_photos);
    QSize size;
    const QImage icon = provider.requestImage(url.mid(imagePrefix().size()), &size, {});
    QCOMPARE(icon.size(), QSize(48, 48));
    QCOMPARE(size, QSize(48, 48));
    QVERIFY(
        provider.requestImage(QStringLiteral("app/org.example.unknown/1"), nullptr, {}).isNull());
    QVERIFY(provider.requestImage(QStringLiteral("app/../org.example.zoo"), nullptr, {}).isNull());
    QVERIFY(provider.requestImage(QStringLiteral("app/org.example.zoo"), nullptr, {}).isNull());
    QVERIFY(provider.requestImage(QStringLiteral("app/org.example.zoo/x"), nullptr, {}).isNull());

    m_model->setFilter(AppListModel::Filter::All);
    const int bare = m_model->rowOf(QStringLiteral("android.ext.shared"));
    QTRY_VERIFY_WITH_TIMEOUT(!m_photos->appIcon(QStringLiteral("com.android.settings")).isNull(),
                             timeoutMs);
    QVERIFY(m_model->index(bare).data(AppListModel::IconUrlRole).toString().isEmpty());
}

// After an update the icon kept its address, so the window went on showing
// the picture it had cached for the old version.
void TestApps::iconAddressChangesWithTheIcon()
{
    QVERIFY(loadAndWait());
    QTRY_VERIFY_WITH_TIMEOUT(!iconUrl(QStringLiteral("org.example.zoo")).isEmpty(), timeoutMs);
    const QString before = iconUrl(QStringLiteral("org.example.zoo"));
    const QString other = iconUrl(QStringLiteral("org.example.uebung"));

    FakeApp zoo = fakeApp(QStringLiteral("org.example.zoo"), QStringLiteral("Zoo"), true, 1);
    zoo.version = QStringLiteral("1.1");
    zoo.icon = iconPng(Qt::red);
    // The phone drops what it reports on its own while an OBEX session runs.
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
    m_phone->confirmInstall(zoo);
    QTRY_VERIFY_WITH_TIMEOUT(!iconUrl(QStringLiteral("org.example.zoo")).isEmpty() &&
                                 iconUrl(QStringLiteral("org.example.zoo")) != before,
                             timeoutMs);
    const QString after = iconUrl(QStringLiteral("org.example.zoo"));
    QVERIFY2(after.startsWith(imagePrefix() + QStringLiteral("app/org.example.zoo/")),
             qPrintable(after));
    QCOMPARE(iconUrl(QStringLiteral("org.example.uebung")), other);
    PhoneImageProvider provider(m_photos);
    QCOMPARE(provider.requestImage(after.mid(imagePrefix().size()), nullptr, {}).pixelColor(0, 0),
             QColor(Qt::red));

    // A new connection may bring another phone with the same package.
    m_model->setConnected(false);
    m_model->setConnected(true);
    QVERIFY(loadAndWait());
    QTRY_VERIFY_WITH_TIMEOUT(!iconUrl(QStringLiteral("org.example.zoo")).isEmpty(), timeoutMs);
    QCOMPARE_NE(iconUrl(QStringLiteral("org.example.zoo")), before);
    QCOMPARE_NE(iconUrl(QStringLiteral("org.example.zoo")), after);
}

void TestApps::startReportsWhetherTheAppOpened()
{
    QVERIFY(loadAndWait());
    QSignalSpy info(m_model.get(), &AppListModel::infoMessage);
    QSignalSpy error(m_model.get(), &AppListModel::errorOccurred);

    QVERIFY(m_model->start(QStringLiteral("org.example.zoo")));
    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QCOMPARE(info.first().first().toString(), QStringLiteral("Zoo opens on the phone."));

    QVERIFY(m_model->start(QStringLiteral("org.example.widget")));
    QTRY_COMPARE_WITH_TIMEOUT(error.count(), 1, timeoutMs);
    QCOMPARE(error.first().first().toString(),
             QStringLiteral("The phone cannot open Wetter. The app has no window of its own."));

    QVERIFY(!m_model->start(QStringLiteral("org.example.unknown")));
    QCOMPARE(m_phone->startRequests(), (QStringList{QStringLiteral("org.example.zoo"),
                                                    QStringLiteral("org.example.widget")}));
}

// The phone asks its user; the app leaves the list only once the phone
// reports it removed.
void TestApps::removalIsAskedAndShownOnceConfirmed()
{
    QVERIFY(loadAndWait());
    QSignalSpy info(m_model.get(), &AppListModel::infoMessage);

    QVERIFY(m_model->uninstall(QStringLiteral("org.example.zoo")));
    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QCOMPARE(info.first().first().toString(),
             QStringLiteral("The phone asks whether to remove Zoo. Confirm it on the phone."));
    QVERIFY(m_model->rowOf(QStringLiteral("org.example.zoo")) >= 0);

    m_phone->confirmUninstall(QStringLiteral("org.example.zoo"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->rowOf(QStringLiteral("org.example.zoo")), -1, timeoutMs);
    QTRY_COMPARE(info.count(), 2);
    QCOMPARE(info.last().first().toString(), QStringLiteral("Zoo is removed from the phone."));
    QCOMPARE(m_model->totalCount(), 4);

    // A removal nobody asked for here changes the list without a message.
    m_phone->confirmUninstall(QStringLiteral("org.example.widget"));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->totalCount(), 3, timeoutMs);
    QCOMPARE(info.count(), 2);
}

void TestApps::apkIsSavedWithProgress()
{
    QVERIFY(loadAndWait());
    QSignalSpy info(m_model.get(), &AppListModel::infoMessage);
    const QString target = m_dir->filePath(QStringLiteral("Zoo.apk"));
    const auto texts = std::make_shared<QStringList>();
    connect(m_model.get(), &AppListModel::transferChanged, this,
            [this, texts]() { texts->append(m_model->transferText()); });

    QVERIFY(m_model->backup(QStringLiteral("org.example.zoo"), QUrl::fromLocalFile(target)));
    QVERIFY(m_model->isTransferActive());
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->isTransferActive(), timeoutMs);

    QCOMPARE(info.count(), 1);
    QCOMPARE(info.first().first().toString(), QStringLiteral("The APK of Zoo is saved."));
    QFile file(target);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), apkData(1000, 1));
    QVERIFY(texts->contains(QStringLiteral("Saving the APK of Zoo: 100 %")));
    QVERIFY(m_model->transferText().isEmpty());
}

void TestApps::apkIsSentAndTheInstallationAnnounced()
{
    QVERIFY(loadAndWait());
    QSignalSpy info(m_model.get(), &AppListModel::infoMessage);
    const QString source = m_dir->filePath(QStringLiteral("Würfel.apk"));
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(apkData(4000, 9)), qint64(4000));
    }

    QVERIFY(m_model->install(QUrl::fromLocalFile(source)));
    QTRY_COMPARE_WITH_TIMEOUT(info.count(), 1, timeoutMs);
    QCOMPARE(
        info.first().first().toString(),
        QStringLiteral("The phone asks whether to install Würfel.apk. Confirm it on the phone."));
    QCOMPARE(m_phone->installRequests().size(), 1);

    FakeApp game = fakeApp(QStringLiteral("org.example.game"), QStringLiteral("Würfel"), true, 5);
    m_phone->confirmInstall(game);
    QTRY_VERIFY_WITH_TIMEOUT(m_model->rowOf(QStringLiteral("org.example.game")) >= 0, timeoutMs);
    QTRY_COMPARE(info.count(), 2);
    QCOMPARE(info.last().first().toString(), QStringLiteral("Würfel is installed on the phone."));
    QCOMPARE(column(*m_model, "label"),
             (QStringList{QStringLiteral("Übungen"), QStringLiteral("Wetter"),
                          QStringLiteral("Würfel"), QStringLiteral("Zoo")}));
    QTRY_VERIFY_WITH_TIMEOUT(!m_photos->appIcon(QStringLiteral("org.example.game")).isNull(),
                             timeoutMs);

    // An update the phone makes on its own changes the row without a message.
    game.version = QStringLiteral("5.1");
    m_phone->confirmInstall(game);
    QTRY_COMPARE_WITH_TIMEOUT(m_model->index(m_model->rowOf(QStringLiteral("org.example.game")))
                                  .data(AppListModel::VersionRole)
                                  .toString(),
                              QStringLiteral("5.1"), timeoutMs);
    QCOMPARE(info.count(), 2);
}

void TestApps::notAnApkIsRefusedWithAText()
{
    QVERIFY(loadAndWait());
    QSignalSpy error(m_model.get(), &AppListModel::errorOccurred);
    const QString source = m_dir->filePath(QStringLiteral("Brief.apk"));
    {
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Sehr geehrte Damen und Herren");
    }

    QVERIFY(m_model->install(QUrl::fromLocalFile(source)));
    QTRY_COMPARE_WITH_TIMEOUT(error.count(), 1, timeoutMs);
    QCOMPARE(error.first().first().toString(),
             QStringLiteral("Brief.apk is no Android app. Choose an APK file that is not empty."));
    QVERIFY(m_phone->installRequests().isEmpty());

    QVERIFY(!m_model->install(QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("gone.apk")))));
    QCOMPARE(error.count(), 2);
}

void TestApps::secondCopyWaitsForTheFirst()
{
    QVERIFY(loadAndWait());
    QSignalSpy error(m_model.get(), &AppListModel::errorOccurred);
    const QUrl first = QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("a.apk")));
    const QUrl second = QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("b.apk")));

    QVERIFY(m_model->backup(QStringLiteral("org.example.zoo"), first));
    QVERIFY(!m_model->backup(QStringLiteral("org.example.uebung"), second));
    QCOMPARE(error.count(), 1);
    QCOMPARE(error.first().first().toString(),
             QStringLiteral("Another transfer is running. Try again when it is done."));
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->isTransferActive(), timeoutMs);
    QVERIFY(QFile::exists(first.toLocalFile()));
    QVERIFY(!QFile::exists(second.toLocalFile()));
}

void TestApps::refusedListShowsWhatToDo()
{
    m_phone->config().scriptedReplies.insert(QStringLiteral("AT*EJAVA=1"),
                                             {QByteArrayLiteral("AT*EJAVA=1\r\nERROR\r\n")});
    QSignalSpy error(m_model.get(), &AppListModel::errorOccurred);

    QVERIFY(m_model->load());
    QTRY_COMPARE_WITH_TIMEOUT(error.count(), 1, timeoutMs);
    QCOMPARE(error.first().first().toString(),
             QStringLiteral("The phone could not do this. Try again."));
    QCOMPARE(m_model->emptyText(), QStringLiteral("The apps cannot be shown."));
    QCOMPARE(m_model->count(), 0);
}

void TestApps::lostConnectionEmptiesTheList()
{
    QVERIFY(loadAndWait());
    QTRY_VERIFY_WITH_TIMEOUT(!m_photos->appIcon(QStringLiteral("org.example.zoo")).isNull(),
                             timeoutMs);

    m_model->setConnected(false);

    QCOMPARE(m_model->count(), 0);
    QCOMPARE(m_model->totalCount(), 0);
    QVERIFY(!m_model->canEdit());
    QVERIFY(m_photos->appIcon(QStringLiteral("org.example.zoo")).isNull());
    QCOMPARE(m_model->emptyText(), QStringLiteral("Connect your phone to see its apps."));
    QVERIFY(!m_model->load());
    QVERIFY(!m_model->start(QStringLiteral("org.example.zoo")));
    QVERIFY(!m_model->install(QUrl::fromLocalFile(m_dir->filePath(QStringLiteral("a.apk")))));
}

QTEST_MAIN(TestApps)

#include "tst_apps.moc"
