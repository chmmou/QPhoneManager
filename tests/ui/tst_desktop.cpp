#include "core/codec/PhoneVCard.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/CallMonitor.h"
#include "core/service/ClipboardService.h"
#include "core/service/ContactsService.h"
#include "core/service/NotificationService.h"
#include "core/store/ContactStore.h"
#include "core/store/ProfileStore.h"
#include "platform/DesktopNotifier.h"
#include "ui/Settings.h"
#include "ui/contacts/PhoneImageProvider.h"
#include "ui/desktop/CallControl.h"
#include "ui/desktop/DesktopIntegration.h"
#include "ui/desktop/NotificationListModel.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QBuffer>
#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <algorithm>
#include <memory>

using namespace qpm::core;
using qpm::DesktopNotifier;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::CallMonitor;
using qpm::core::service::ClipboardService;
using qpm::core::service::ContactsService;
using qpm::core::service::NotificationService;
using qpm::test::FakeNotification;
using qpm::test::FakePhone;
using qpm::test::FakePhoneConfig;
using qpm::test::waitFor;
using qpm::ui::CallControl;
using qpm::ui::DesktopIntegration;
using qpm::ui::NotificationListModel;
using qpm::ui::PhotoSource;
using qpm::ui::Settings;

namespace {

constexpr int timeoutMs = 20000;

FakeNotification chat(int id, const QString &title)
{
    FakeNotification notification;
    notification.id = id;
    notification.title = title;
    notification.text = QStringLiteral("Bis später");
    notification.actions = {QStringLiteral("Antworten"), QStringLiteral("Gelesen")};
    notification.posted = QDateTime(QDate(2026, 11, 10), QTime(9, 30), QTimeZone::UTC).addSecs(id);
    return notification;
}

QByteArray jpeg()
{
    QImage image(64, 48, QImage::Format_RGB32);
    image.fill(QColor(0, 106, 106));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "JPEG");
    return bytes;
}

QVariant role(const QAbstractItemModel &model, int row, const QByteArray &name)
{
    return model.data(model.index(row, 0), model.roleNames().key(name));
}

QByteArray contact(const QString &name, const QString &number)
{
    codec::Contact entry;
    entry.luid = 1;
    entry.givenName = name;
    entry.displayName = name;
    codec::ContactValue phone;
    phone.dataId = 11;
    phone.type = codec::phoneType::mobile;
    phone.value = number;
    entry.phones = {phone};
    return codec::encodeAsPhoneSendsIt(entry);
}

/**
 * Answers the freedesktop notification service and records what it showed.
 * Qt D-Bus may call handleMessage() from its own thread, so every member is
 * guarded by the mutex.
 */
class FakeNotificationService : public QDBusVirtualObject
{
    Q_OBJECT

public:
    using QDBusVirtualObject::QDBusVirtualObject;

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        QMutexLocker locker(&m_mutex);
        QDBusConnection bus(connection);
        if (message.member() == QLatin1StringView("GetCapabilities")) {
            bus.send(message.createReply(QStringList{QStringLiteral("actions")}));
        } else if (message.member() == QLatin1StringView("Notify")) {
            m_actions.append(message.arguments().value(5).toStringList());
            m_bodies.append(message.arguments().value(4).toString());
            bus.send(message.createReply(uint(m_actions.size())));
        } else if (message.member() == QLatin1StringView("CloseNotification")) {
            m_closed.append(message.arguments().value(0).toUInt());
            bus.send(message.createReply());
        } else {
            bus.send(message.createErrorReply(
                QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"), {}));
        }
        return true;
    }

    QList<QStringList> actions() const
    {
        QMutexLocker locker(&m_mutex);
        return m_actions;
    }

    QStringList bodies() const
    {
        QMutexLocker locker(&m_mutex);
        return m_bodies;
    }

    QList<uint> closed() const
    {
        QMutexLocker locker(&m_mutex);
        return m_closed;
    }

private:
    mutable QMutex m_mutex;
    QList<QStringList> m_actions;
    QStringList m_bodies;
    QList<uint> m_closed;
};

} // namespace

// The notifications, the call and the clipboard for the window and the
// desktop, against the fake phone: the list model with its pictures and
// actions, the call control with the caller's name, the clipboard shared
// both ways, and the desktop notifications with their buttons, the latter on
// a notification service of the test's own.
class TestDesktop : public QObject
{
    Q_OBJECT

public:
    explicit TestDesktop(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void listShowsTheNotificationsWithTheirRoles();
    void listFollowsThePhoneRowByRow();
    void pictureIsShownUnderItsAddress();
    void dismissAndActionReachThePhone();
    void missingAccessSaysWhatToDo();
    void callShowsTheCallerAndIsControlled();
    void clipboardIsSharedBothWaysWithoutAnEcho();
    void clipboardStaysWhileSharingIsOff();
    void desktopNotificationOffersTheActionsOfThePhone();
    void desktopNotificationsStayOffWhenSwitchedOff();

private:
    [[nodiscard]] bool connectPhone();
    [[nodiscard]] bool startNotificationService();

    QTemporaryDir m_busDir;
    QProcess m_daemon;
    QString m_busAddress;
    int m_round = 0;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<NotificationService> m_notifications;
    std::unique_ptr<ClipboardService> m_clipboard;
    std::shared_ptr<PhotoSource> m_photos;
    std::unique_ptr<NotificationListModel> m_model;
    std::unique_ptr<FakeNotificationService> m_service;
    std::unique_ptr<DesktopNotifier> m_notifier;
};

void TestDesktop::initTestCase()
{
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedKingdom));
    QVERIFY(m_busDir.isValid());
    const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
    if (daemon.isEmpty())
        return;
    // A private bus without service directories, so that nothing on this
    // machine shows the notifications of the test.
    const QString config = m_busDir.filePath(QStringLiteral("bus.conf"));
    QFile file(config);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QStringLiteral("<busconfig><type>session</type><listen>unix:dir=%1</listen>"
                              "<auth>EXTERNAL</auth><policy context=\"default\">"
                              "<allow send_destination=\"*\" eavesdrop=\"true\"/>"
                              "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy></busconfig>")
                   .arg(m_busDir.path())
                   .toUtf8());
    file.close();
    m_daemon.start(daemon, {QStringLiteral("--config-file=") + config, QStringLiteral("--nofork"),
                            QStringLiteral("--print-address")});
    QVERIFY(m_daemon.waitForStarted());
    QTRY_VERIFY_WITH_TIMEOUT(m_daemon.canReadLine(), 10000);
    m_busAddress = QString::fromUtf8(m_daemon.readLine()).trimmed();
}

void TestDesktop::cleanupTestCase()
{
    if (m_daemon.state() != QProcess::NotRunning) {
        m_daemon.terminate();
        QVERIFY(m_daemon.waitForFinished());
    }
}

void TestDesktop::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    m_settings = std::make_unique<Settings>(m_dir->filePath(QStringLiteral("settings.ini")));
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_connection = std::make_unique<PhoneConnection>();
    m_notifications = std::make_unique<NotificationService>(m_connection.get());
    m_clipboard = std::make_unique<ClipboardService>(m_connection.get());
    m_photos = std::make_shared<PhotoSource>();
    m_model = std::make_unique<NotificationListModel>(m_notifications.get(), m_photos);
}

void TestDesktop::cleanup()
{
    if (m_phone)
        QCOMPARE(m_phone->protocolErrors(), QStringList());
    m_notifier.reset();
    if (m_round > 0) {
        QDBusConnection::disconnectFromBus(QStringLiteral("client-%1").arg(m_round));
        QDBusConnection::disconnectFromBus(QStringLiteral("service-%1").arg(m_round));
    }
    m_service.reset();
    m_model.reset();
    m_clipboard.reset();
    m_notifications.reset();
    m_connection.reset();
    m_phone.reset();
    m_settings.reset();
    m_dir.reset();
}

bool TestDesktop::connectPhone()
{
    PhoneConnection::Options options;
    options.notifications = true;
    options.clipboardWatch = m_settings->clipboardSync();
    m_connection->setOptions(options);
    QObject::connect(m_connection.get(), &PhoneConnection::stateChanged, m_model.get(), [this]() {
        m_model->setConnected(m_connection->state() == PhoneConnection::State::Ready ||
                              m_connection->state() == PhoneConnection::State::Busy);
    });
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    return QTest::qWaitFor(
        [this]() { return m_connection->state() == PhoneConnection::State::Ready; }, timeoutMs);
}

bool TestDesktop::startNotificationService()
{
    if (m_busAddress.isEmpty())
        return false;
    ++m_round;
    QDBusConnection serviceBus =
        QDBusConnection::connectToBus(m_busAddress, QStringLiteral("service-%1").arg(m_round));
    QDBusConnection clientBus =
        QDBusConnection::connectToBus(m_busAddress, QStringLiteral("client-%1").arg(m_round));
    m_service = std::make_unique<FakeNotificationService>();
    if (!serviceBus.registerVirtualObject(QStringLiteral("/org/freedesktop/Notifications"),
                                          m_service.get()) ||
        !serviceBus.registerService(QStringLiteral("org.freedesktop.Notifications"))) {
        return false;
    }
    m_notifier = std::make_unique<DesktopNotifier>(clientBus);
    return true;
}

void TestDesktop::listShowsTheNotificationsWithTheirRoles()
{
    FakeNotification ongoing = chat(2, QStringLiteral("Download"));
    ongoing.flags = 0x02;
    ongoing.actions.clear();
    m_phone->setNotifications({chat(1, QStringLiteral("Anna Becker")), ongoing});

    QVERIFY(connectPhone());

    QTRY_COMPARE_WITH_TIMEOUT(m_model->count(), 2, timeoutMs);
    QCOMPARE(role(*m_model, 0, "title").toString(), QStringLiteral("Download"));
    QCOMPARE(role(*m_model, 1, "title").toString(), QStringLiteral("Anna Becker"));
    QCOMPARE(role(*m_model, 1, "app").toString(), QStringLiteral("Chat"));
    QCOMPARE(role(*m_model, 1, "text").toString(), QStringLiteral("Bis später"));
    QCOMPARE(role(*m_model, 1, "notificationKey").toString(),
             QStringLiteral("org.example.chat|1|"));
    QCOMPARE(role(*m_model, 1, "actions").toStringList(),
             (QStringList{QStringLiteral("Antworten"), QStringLiteral("Gelesen")}));
    QVERIFY(role(*m_model, 1, "canDismiss").toBool());
    QVERIFY(!role(*m_model, 0, "canDismiss").toBool());
    QVERIFY(!role(*m_model, 1, "timeText").toString().isEmpty());
    QVERIFY(role(*m_model, 1, "imageUrl").toString().isEmpty());
    QVERIFY(m_model->canEdit());
    QCOMPARE(m_model->rowOf(QStringLiteral("org.example.chat|1|")), 1);
}

void TestDesktop::listFollowsThePhoneRowByRow()
{
    m_phone->setNotifications({chat(1, QStringLiteral("Anna"))});
    QVERIFY(connectPhone());
    QTRY_COMPARE_WITH_TIMEOUT(m_model->count(), 1, timeoutMs);
    QSignalSpy inserted(m_model.get(), &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(m_model.get(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(m_model.get(), &QAbstractItemModel::modelReset);

    m_phone->postNotification(chat(5, QStringLiteral("Bernd")));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->count(), 2, timeoutMs);
    m_phone->removeNotification(chat(1, QString()).key());
    QTRY_COMPARE_WITH_TIMEOUT(m_model->count(), 1, timeoutMs);

    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 0);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(role(*m_model, 0, "title").toString(), QStringLiteral("Bernd"));
}

void TestDesktop::pictureIsShownUnderItsAddress()
{
    FakeNotification photo = chat(3, QStringLiteral("Foto"));
    photo.picture = jpeg();
    m_phone->setNotifications({photo});

    QVERIFY(connectPhone());

    QTRY_VERIFY_WITH_TIMEOUT(!role(*m_model, 0, "imageUrl").toString().isEmpty(), timeoutMs);
    const QString url = role(*m_model, 0, "imageUrl").toString();
    QVERIFY(url.startsWith(QStringLiteral("image://qpm/thumb/")));
    const QImage shown = m_photos->thumbnail(url.section(u'/', -1).toULongLong());
    QCOMPARE(shown.size(), QSize(64, 48));
}

void TestDesktop::dismissAndActionReachThePhone()
{
    const FakeNotification message = chat(4, QStringLiteral("Anna"));
    m_phone->setNotifications({message});
    QVERIFY(connectPhone());
    QTRY_COMPARE_WITH_TIMEOUT(m_model->count(), 1, timeoutMs);
    QSignalSpy info(m_model.get(), &NotificationListModel::infoMessage);

    QVERIFY(m_model->trigger(message.key(), QStringLiteral("Gelesen")));
    QTRY_COMPARE_WITH_TIMEOUT(m_phone->notificationActions().size(), 1, timeoutMs);
    QTRY_COMPARE(info.count(), 1);
    QVERIFY(m_model->dismiss(message.key()));

    QTRY_COMPARE_WITH_TIMEOUT(m_model->count(), 0, timeoutMs);
    QVERIFY(m_phone->notifications().isEmpty());
    QCOMPARE(m_phone->notificationActions().constFirst().second, QStringLiteral("Gelesen"));
}

void TestDesktop::missingAccessSaysWhatToDo()
{
    m_phone->config().notificationAccess = FakePhoneConfig::NotificationAccess::Disabled;
    QVERIFY(!m_model->canEdit());
    QVERIFY(m_model->emptyText().startsWith(QStringLiteral("Connect your phone")));

    QVERIFY(connectPhone());

    QTRY_VERIFY_WITH_TIMEOUT(m_model->isAccessMissing(), timeoutMs);
    QVERIFY(!m_model->canEdit());
    QVERIFY(m_model->emptyText().contains(QStringLiteral("notification access")));
    QVERIFY(!m_model->dismiss(QStringLiteral("org.example.chat|1|")));
    QVERIFY(m_model->openAccessSettings());
    QTRY_VERIFY_WITH_TIMEOUT(
        m_phone->receivedCommands().contains(QStringLiteral("AT*NOTISHOWSETTING")), timeoutMs);
}

void TestDesktop::callShowsTheCallerAndIsControlled()
{
    m_phone->setObjects(QStringLiteral("pb"), {{1, contact(QStringLiteral("Zoë Örtel"),
                                                           QStringLiteral("+49 40 2000006"))}});
    QVERIFY(connectPhone());
    auto profile = std::make_unique<store::ProfileStore>(m_dir->path(), QStringLiteral("dev-1"));
    QVERIFY(profile->open().hasValue());
    auto contacts = std::make_shared<store::ContactStore>(profile.get());
    ContactsService sync(m_connection.get());
    sync.setStore(contacts);
    const QFuture<Result<store::ContactChanges>> synced = sync.sync();
    QVERIFY(waitFor(synced, timeoutMs));
    CallMonitor monitor(m_connection.get());
    monitor.setContactStore(contacts);
    CallControl control(&monitor);
    QCOMPARE(control.statusText(), QStringLiteral("No call"));
    QVERIFY(!control.answer());

    m_phone->setCallState(6, QStringLiteral("+49402000006"));
    QTRY_COMPARE_WITH_TIMEOUT(control.state(), CallControl::State::Ringing, timeoutMs);
    QVERIFY(control.isActive());
    QCOMPARE(control.callerText(), QStringLiteral("Zoë Örtel"));
    QCOMPARE(control.numberText(), QStringLiteral("+49 40 2000006"));
    QCOMPARE(control.statusText(), QStringLiteral("Incoming call"));
    QVERIFY(!control.setMuted(true));
    QVERIFY(control.answer());
    QTRY_COMPARE_WITH_TIMEOUT(control.state(), CallControl::State::Active, timeoutMs);
    QCOMPARE(control.statusText(), QStringLiteral("Call in progress"));
    QVERIFY(control.setMuted(true));
    QTRY_VERIFY_WITH_TIMEOUT(control.isMuted(), timeoutMs);
    QVERIFY(control.setSpeakerOn(true));
    QTRY_VERIFY_WITH_TIMEOUT(control.isSpeakerOn(), timeoutMs);
    QVERIFY(control.hangUp());

    QTRY_COMPARE_WITH_TIMEOUT(control.state(), CallControl::State::Idle, timeoutMs);
    QVERIFY(!control.isMuted());
    m_phone->setCallState(6, QString());
    QTRY_COMPARE_WITH_TIMEOUT(control.callerText(), QStringLiteral("Unknown number"), timeoutMs);
    QVERIFY(control.numberText().isEmpty());
    monitor.setContactStore(nullptr);
}

void TestDesktop::clipboardIsSharedBothWaysWithoutAnEcho()
{
    m_settings->setClipboardSync(true);
    DesktopIntegration integration(m_settings.get(), m_notifications.get(), m_clipboard.get());
    QVERIFY(connectPhone());
    QVERIFY(m_phone->clipboardWatched());
    QClipboard *board = QGuiApplication::clipboard();

    board->setText(QStringLiteral("Vom Rechner, mit Komma"));
    QTRY_COMPARE_WITH_TIMEOUT(m_phone->config().clipboard, QStringLiteral("Vom Rechner, mit Komma"),
                              timeoutMs);
    m_phone->copyOnPhone(QStringLiteral("Vom Handy: Grüße"));
    QTRY_COMPARE_WITH_TIMEOUT(board->text(), QStringLiteral("Vom Handy: Grüße"), timeoutMs);

    const QStringList commands = m_phone->receivedCommands();
    const auto writes =
        std::count_if(commands.cbegin(), commands.cend(), [](const QString &command) {
            return command.startsWith(QLatin1StringView("AT*CLIPBOARD="));
        });
    QCOMPARE(writes, 1);
}

void TestDesktop::clipboardStaysWhileSharingIsOff()
{
    DesktopIntegration integration(m_settings.get(), m_notifications.get(), m_clipboard.get());
    QVERIFY(connectPhone());
    QClipboard *board = QGuiApplication::clipboard();
    board->setText(QStringLiteral("Bleibt hier"));
    m_phone->copyOnPhone(QStringLiteral("Bleibt am Handy"));

    // Switching it on tells the phone at once; the next copy is shared.
    m_settings->setClipboardSync(true);
    QTRY_VERIFY_WITH_TIMEOUT(m_phone->clipboardWatched(), timeoutMs);
    QCOMPARE(board->text(), QStringLiteral("Bleibt hier"));
    QVERIFY(m_phone->config().clipboard != QStringLiteral("Bleibt hier"));
    board->setText(QStringLiteral("Jetzt geteilt"));
    QTRY_COMPARE_WITH_TIMEOUT(m_phone->config().clipboard, QStringLiteral("Jetzt geteilt"),
                              timeoutMs);
}

void TestDesktop::desktopNotificationOffersTheActionsOfThePhone()
{
    if (!startNotificationService())
        QSKIP("dbus-daemon is not installed");
    DesktopIntegration integration(m_settings.get(), m_notifications.get(), m_clipboard.get());
    integration.setNotifier(m_notifier.get());
    QSignalSpy requested(&integration, &DesktopIntegration::notificationsRequested);
    QVERIFY(connectPhone());
    QTRY_COMPARE_WITH_TIMEOUT(m_notifications->access(), NotificationService::Access::Granted,
                              timeoutMs);
    const FakeNotification message = chat(8, QStringLiteral("Anna"));

    m_phone->postNotification(message);

    QTRY_COMPARE_WITH_TIMEOUT(m_service->actions().size(), 1, timeoutMs);
    QCOMPARE(m_service->actions().constFirst(),
             (QStringList{QStringLiteral("default"), QStringLiteral("Show"),
                          QStringLiteral("action-0"), QStringLiteral("Antworten"),
                          QStringLiteral("action-1"), QStringLiteral("Gelesen"),
                          QStringLiteral("dismiss"), QStringLiteral("Dismiss on the phone")}));
    QCOMPARE(m_service->bodies().constFirst(), QStringLiteral("Chat\nBis später"));
    QDBusConnection serviceBus(QStringLiteral("service-%1").arg(m_round));
    const auto invoke = [&serviceBus](const QString &key) {
        QDBusMessage signal = QDBusMessage::createSignal(
            QStringLiteral("/org/freedesktop/Notifications"),
            QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("ActionInvoked"));
        signal << 1u << key;
        return serviceBus.send(signal);
    };
    QVERIFY(invoke(QStringLiteral("action-1")));
    QTRY_COMPARE_WITH_TIMEOUT(m_phone->notificationActions().size(), 1, timeoutMs);
    QCOMPARE(m_phone->notificationActions().constFirst().second, QStringLiteral("Gelesen"));
    QVERIFY(invoke(QStringLiteral("default")));
    QTRY_COMPARE(requested.count(), 1);
    QVERIFY(invoke(QStringLiteral("dismiss")));

    QTRY_VERIFY_WITH_TIMEOUT(m_phone->notifications().isEmpty(), timeoutMs);
    // The phone reports the removal, and the desktop notification closes with it.
    QTRY_COMPARE_WITH_TIMEOUT(m_service->closed(), QList<uint>{1u}, timeoutMs);
}

void TestDesktop::desktopNotificationsStayOffWhenSwitchedOff()
{
    if (!startNotificationService())
        QSKIP("dbus-daemon is not installed");
    m_settings->setNotificationsEnabled(false);
    DesktopIntegration integration(m_settings.get(), m_notifications.get(), m_clipboard.get());
    integration.setNotifier(m_notifier.get());
    QVERIFY(connectPhone());
    QTRY_COMPARE_WITH_TIMEOUT(m_notifications->access(), NotificationService::Access::Granted,
                              timeoutMs);

    m_phone->postNotification(chat(9, QStringLiteral("Bernd")));
    QTRY_COMPARE_WITH_TIMEOUT(m_model->count(), 1, timeoutMs);
    m_settings->setNotificationsEnabled(true);
    FakeNotification later = chat(10, QStringLiteral("Clara"));
    later.text = QStringLiteral("Danach");
    m_phone->postNotification(later);

    // Messages on the bus keep their order, so the first one shown is the later post.
    QTRY_COMPARE_WITH_TIMEOUT(m_service->bodies().size(), 1, timeoutMs);
    QCOMPARE(m_service->bodies().constFirst(), QStringLiteral("Chat\nDanach"));
}

QTEST_MAIN(TestDesktop)

#include "tst_desktop.moc"
