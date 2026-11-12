#include "core/codec/AtText.h"
#include "core/codec/NotificationCodec.h"
#include "core/protocol/ObexClient.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/NotificationService.h"

#include "FakePhone.h"
#include "WaitFor.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

#include <memory>

using namespace qpm::core;
using namespace qpm::core::codec;
using qpm::core::protocol::ObexClient;
using qpm::core::protocol::PhoneConnection;
using qpm::core::service::NotificationService;
using qpm::test::FakeNotification;
using qpm::test::FakePhone;
using qpm::test::FakePhoneConfig;
using qpm::test::waitFor;

namespace {

constexpr int timeoutMs = 20000;

FakeNotification chat(int id, const QString &title, int minutesAgo)
{
    FakeNotification notification;
    notification.id = id;
    notification.title = title;
    notification.text = QStringLiteral("Bis später, Grüße");
    notification.actions = {QStringLiteral("Antworten"), QStringLiteral("Gelesen")};
    notification.posted = QDateTime(QDate(2026, 11, 12), QTime(9, 0), QTimeZone::UTC)
                              .addSecs(-60 * qint64(minutesAgo));
    return notification;
}

QString record(const QStringList &fields)
{
    return fields.join(u',');
}

QStringList validFields()
{
    return {QStringLiteral("-7"),
            encodeQp(QStringLiteral("tag,1")),
            QStringLiteral("org.example.mail"),
            QStringLiteral("1"),
            QStringLiteral("20261112T091500"),
            QString(),
            QStringLiteral("24"),
            QStringLiteral("1"),
            QStringLiteral("-1"),
            encodeQp(QStringLiteral("Post: Büro")),
            QString(),
            encodeQp(QStringLiteral("Anna Becker")),
            encodeQp(QStringLiteral("Termin morgen\nUm 9 Uhr")),
            encodeQp(QStringLiteral("Antworten|Archivieren")),
            QString(),
            QString(),
            QString(),
            QStringLiteral("1")};
}

} // namespace

// The notification records of the phone and the service that keeps the list,
// against the fake phone: reading, the reports the phone sends on its own,
// dismissing, actions, pictures and the refusals when the phone app may not
// read notifications.
class TestNotifications : public QObject
{
    Q_OBJECT

public:
    explicit TestNotifications(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void recordIsReadWithEveryField();
    void malformedRecordsAreRefused_data();
    void malformedRecordsAreRefused();
    void overlongTextsAreCut();
    void removalReportIsRead();
    void commandsEscapeTheTagAndOnlyNameOneNotification();

    void listIsReadWhenTheConnectionIsReady();
    void postedAndRemovedNotificationsFollowThePhone();
    void listAfterObexAnnouncesOnlyNewOnes();
    void dismissRemovesTheNotificationOnThePhone();
    void ongoingNotificationIsNotDismissed();
    void actionAndTapReachThePhone();
    void unknownActionOrNotificationSendsNothing();
    void pictureIsFetchedWithinItsLimit();
    void missingAccessIsReportedAsItsOwnError_data();
    void missingAccessIsReportedAsItsOwnError();
    void listEmptiesWhenTheConnectionEnds();
    void listEmptiesWhenTheAccessIsWithdrawn();

private:
    [[nodiscard]] bool connect(bool notificationsOption = true);

    std::unique_ptr<FakePhone> m_phone;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<NotificationService> m_service;
};

void TestNotifications::init()
{
    m_phone = std::make_unique<FakePhone>();
    QVERIFY(m_phone->listen());
    m_connection = std::make_unique<PhoneConnection>();
    m_service = std::make_unique<NotificationService>(m_connection.get());
}

void TestNotifications::cleanup()
{
    if (m_phone)
        QCOMPARE(m_phone->protocolErrors(), QStringList());
    m_service.reset();
    m_connection.reset();
    m_phone.reset();
}

bool TestNotifications::connect(bool notificationsOption)
{
    PhoneConnection::Options options;
    options.notifications = notificationsOption;
    m_connection->setOptions(options);
    m_connection->connectTcp(QStringLiteral("127.0.0.1"), m_phone->port());
    return QTest::qWaitFor(
        [this]() { return m_connection->state() == PhoneConnection::State::Ready; }, timeoutMs);
}

void TestNotifications::recordIsReadWithEveryField()
{
    const Result<PhoneNotification> parsed = parseNotificationRecord(record(validFields()));

    QVERIFY2(parsed.hasValue(), qPrintable(parsed.error().message()));
    const PhoneNotification &notification = parsed.value();
    QCOMPARE(notification.id, -7);
    QCOMPARE(notification.tag, QStringLiteral("tag,1"));
    QCOMPARE(notification.package, QStringLiteral("org.example.mail"));
    QVERIFY(notification.isNew);
    QCOMPARE(notification.postTime, QDateTime(QDate(2026, 11, 12), QTime(9, 15)));
    QCOMPARE(notification.appLabel, QStringLiteral("Post: Büro"));
    QCOMPARE(notification.title, QStringLiteral("Anna Becker"));
    QCOMPARE(notification.text, QStringLiteral("Termin morgen\nUm 9 Uhr"));
    QCOMPARE(notification.actions,
             (QStringList{QStringLiteral("Antworten"), QStringLiteral("Archivieren")}));
    QVERIFY(notification.hasPicture);
    QVERIFY(!notification.ongoing);
    QCOMPARE(notification.key(), QStringLiteral("org.example.mail|-7|tag,1"));
}

void TestNotifications::malformedRecordsAreRefused_data()
{
    QTest::addColumn<int>("field");
    QTest::addColumn<QString>("value");
    QTest::addColumn<int>("code");

    QTest::newRow("id no number") << 0 << QStringLiteral("x1") << int(ErrorCode::ProtocolError);
    QTest::newRow("id above 32 bit")
        << 0 << QStringLiteral("4294967296") << int(ErrorCode::ProtocolError);
    QTest::newRow("package with a slash")
        << 2 << QStringLiteral("org/example") << int(ErrorCode::ProtocolError);
    QTest::newRow("empty package") << 2 << QString() << int(ErrorCode::ProtocolError);
    QTest::newRow("tag too long") << 1 << QString(600, u't') << int(ErrorCode::LimitExceeded);
    QTest::newRow("too few fields") << -1 << QString() << int(ErrorCode::ProtocolError);
}

void TestNotifications::malformedRecordsAreRefused()
{
    QFETCH(int, field);
    QFETCH(QString, value);
    QFETCH(int, code);
    QStringList fields = validFields();
    if (field < 0)
        fields.removeLast();
    else
        fields[field] = value;

    const Result<PhoneNotification> parsed = parseNotificationRecord(record(fields));

    QVERIFY(!parsed.hasValue());
    QCOMPARE(int(parsed.error().code()), code);
}

void TestNotifications::overlongTextsAreCut()
{
    QStringList fields = validFields();
    fields[9] = QString(5000, u'l');
    fields[11] = QString(5000, u't');
    fields[12] = QString(50000, u'x');
    QStringList actions;
    for (int i = 0; i < 20; ++i)
        actions.append(QStringLiteral("Aktion %1").arg(i));
    actions.append(QString(400, u'a'));
    fields[13] = encodeQp(actions.join(u'|'));

    const Result<PhoneNotification> parsed = parseNotificationRecord(record(fields));

    QVERIFY(parsed.hasValue());
    QCOMPARE(parsed.value().appLabel.size(), maxNotificationLabelLength);
    QCOMPARE(parsed.value().title.size(), maxNotificationTitleLength);
    QCOMPARE(parsed.value().text.size(), maxNotificationTextLength);
    QCOMPARE(parsed.value().actions.size(), maxNotificationActions);
}

void TestNotifications::removalReportIsRead()
{
    const Result<NotificationEvent> removed =
        parseNotificationEvent(QStringLiteral("*NOTIFY: 12,,org.example.chat"));
    const Result<NotificationEvent> posted =
        parseNotificationEvent(QStringLiteral("*NOTIFY: ") + record(validFields()));
    const Result<NotificationEvent> other =
        parseNotificationEvent(QStringLiteral("*NOTIR: 12,,org.example.chat"));

    QVERIFY(removed.hasValue());
    QCOMPARE(removed.value().kind, NotificationEvent::Kind::Removed);
    QCOMPARE(removed.value().notification.key(), QStringLiteral("org.example.chat|12|"));
    QVERIFY(posted.hasValue());
    QCOMPARE(posted.value().kind, NotificationEvent::Kind::Posted);
    QVERIFY(!other.hasValue());
}

void TestNotifications::commandsEscapeTheTagAndOnlyNameOneNotification()
{
    PhoneNotification notification;
    notification.id = 4;
    notification.tag = QStringLiteral("a,b=c");
    notification.package = QStringLiteral("org.example.chat");
    notification.actions = {QStringLiteral("Antworten: kurz")};

    QCOMPARE(dismissNotificationCommand(notification).value(),
             QStringLiteral("AT*NOTIC=4,a=2Cb=3Dc,org.example.chat"));
    QCOMPARE(notificationActionCommand(notification, QString()).value(),
             QStringLiteral("AT*NOTIACTION=4,a=2Cb=3Dc,org.example.chat"));
    QCOMPARE(notificationActionCommand(notification, QStringLiteral("Antworten: kurz")).value(),
             QStringLiteral("AT*NOTIACTION=4,a=2Cb=3Dc,org.example.chat,Antworten=3A kurz"));
    QCOMPARE(notificationImageName(notification).value(),
             QStringLiteral("\\notificationimage\\org.example.chat\\4\\512\\512"));
    QCOMPARE(notificationActionCommand(notification, QStringLiteral("Löschen")).error().code(),
             ErrorCode::InvalidInput);
    notification.package = QString();
    QCOMPARE(dismissNotificationCommand(notification).error().code(), ErrorCode::InvalidInput);
    QCOMPARE(notificationImageName(notification).error().code(), ErrorCode::InvalidInput);
}

void TestNotifications::listIsReadWhenTheConnectionIsReady()
{
    m_phone->setNotifications(
        {chat(1, QStringLiteral("Anna"), 10), chat(2, QStringLiteral("Jörg"), 2)});
    QSignalSpy posted(m_service.get(), &NotificationService::notificationPosted);

    QVERIFY(connect());

    QTRY_COMPARE_WITH_TIMEOUT(m_service->notifications().size(), 2, timeoutMs);
    QCOMPARE(m_service->access(), NotificationService::Access::Granted);
    QCOMPARE(m_service->notifications().at(0).title, QStringLiteral("Jörg"));
    QCOMPARE(m_service->notifications().at(1).title, QStringLiteral("Anna"));
    QCOMPARE(m_service->notifications().at(0).text, QStringLiteral("Bis später, Grüße"));
    QVERIFY(m_phone->receivedCommands().contains(QStringLiteral("AT*NOTIFY=2")));
    QVERIFY(m_phone->notificationsDetailed());
    // The list that was there before connecting is no news.
    QCOMPARE(posted.count(), 0);
}

void TestNotifications::postedAndRemovedNotificationsFollowThePhone()
{
    QVERIFY(connect());
    QTRY_COMPARE_WITH_TIMEOUT(m_service->access(), NotificationService::Access::Granted, timeoutMs);
    QSignalSpy posted(m_service.get(), &NotificationService::notificationPosted);
    QSignalSpy changed(m_service.get(), &NotificationService::notificationsChanged);

    FakeNotification message = chat(5, QStringLiteral("Clara"), 0);
    m_phone->postNotification(message);

    QTRY_COMPARE_WITH_TIMEOUT(posted.count(), 1, timeoutMs);
    QCOMPARE(posted.first().first().value<PhoneNotification>().title, QStringLiteral("Clara"));
    QCOMPARE(m_service->notifications().size(), 1);

    message.text = QStringLiteral("Zwei neue Nachrichten");
    m_phone->postNotification(message);
    QTRY_COMPARE_WITH_TIMEOUT(m_service->notifications().constFirst().text,
                              QStringLiteral("Zwei neue Nachrichten"), timeoutMs);
    QCOMPARE(posted.count(), 1);

    m_phone->removeNotification(message.key());
    QTRY_VERIFY_WITH_TIMEOUT(m_service->notifications().isEmpty(), timeoutMs);
    QVERIFY(changed.count() >= 3);
}

void TestNotifications::listAfterObexAnnouncesOnlyNewOnes()
{
    m_phone->setNotifications({chat(1, QStringLiteral("Anna"), 10)});
    QVERIFY(connect());
    QTRY_COMPARE_WITH_TIMEOUT(m_service->notifications().size(), 1, timeoutMs);
    QSignalSpy posted(m_service.get(), &NotificationService::notificationPosted);
    // During an OBEX session the phone drops its reports, so only the list shows the change.
    m_phone->setNotifications(
        {chat(1, QStringLiteral("Anna"), 10), chat(3, QStringLiteral("Bernd"), 1)});

    const QFuture<Result<void>> session =
        m_connection->withObex<void>(ObexClient::Target::IrmcSync, [](ObexClient &) {
            return QtFuture::makeReadyValueFuture(Result<void>());
        });
    QVERIFY(waitFor(session, timeoutMs));

    QTRY_COMPARE_WITH_TIMEOUT(m_service->notifications().size(), 2, timeoutMs);
    QCOMPARE(posted.count(), 1);
    QCOMPARE(posted.first().first().value<PhoneNotification>().title, QStringLiteral("Bernd"));
}

void TestNotifications::dismissRemovesTheNotificationOnThePhone()
{
    FakeNotification tagged = chat(9, QStringLiteral("Anna"), 1);
    tagged.tag = QStringLiteral("thread:7,a");
    m_phone->setNotifications({tagged, chat(2, QStringLiteral("Jörg"), 2)});
    QVERIFY(connect());
    QTRY_COMPARE_WITH_TIMEOUT(m_service->notifications().size(), 2, timeoutMs);

    const QFuture<Result<void>> dismissed = m_service->dismiss(tagged.key());
    QVERIFY(waitFor(dismissed, timeoutMs));

    QVERIFY2(dismissed.result().hasValue(), qPrintable(dismissed.result().error().message()));
    QVERIFY(m_phone->receivedCommands().contains(
        QStringLiteral("AT*NOTIC=9,thread=3A7=2Ca,org.example.chat")));
    QCOMPARE(m_phone->notifications().size(), 1);
    QTRY_COMPARE(m_service->notifications().size(), 1);
    QCOMPARE(m_service->notifications().constFirst().title, QStringLiteral("Jörg"));
    QVERIFY(!m_phone->receivedCommands().contains(QStringLiteral("AT*NOTIC")));
}

void TestNotifications::ongoingNotificationIsNotDismissed()
{
    FakeNotification download = chat(3, QStringLiteral("Download"), 0);
    download.flags = 0x02;
    m_phone->setNotifications({download});
    QVERIFY(connect());
    QTRY_COMPARE_WITH_TIMEOUT(m_service->notifications().size(), 1, timeoutMs);
    m_phone->clearLog();

    const QFuture<Result<void>> dismissed = m_service->dismiss(download.key());
    QVERIFY(waitFor(dismissed, timeoutMs));

    QCOMPARE(dismissed.result().error().code(), ErrorCode::Rejected);
    QVERIFY(m_phone->receivedCommands().isEmpty());
    QCOMPARE(m_service->notifications().size(), 1);
}

void TestNotifications::actionAndTapReachThePhone()
{
    const FakeNotification message = chat(4, QStringLiteral("Anna"), 0);
    m_phone->setNotifications({message});
    QVERIFY(connect());
    QTRY_COMPARE_WITH_TIMEOUT(m_service->notifications().size(), 1, timeoutMs);

    const QFuture<Result<void>> action =
        m_service->trigger(message.key(), QStringLiteral("Gelesen"));
    QVERIFY(waitFor(action, timeoutMs));
    const QFuture<Result<void>> tap = m_service->trigger(message.key(), QString());
    QVERIFY(waitFor(tap, timeoutMs));

    QVERIFY(action.result().hasValue());
    QVERIFY(tap.result().hasValue());
    const QList<std::pair<QString, QString>> expected = {{message.key(), QStringLiteral("Gelesen")},
                                                         {message.key(), QString()}};
    QCOMPARE(m_phone->notificationActions(), expected);
}

void TestNotifications::unknownActionOrNotificationSendsNothing()
{
    const FakeNotification message = chat(4, QStringLiteral("Anna"), 0);
    m_phone->setNotifications({message});
    QVERIFY(connect());
    QTRY_COMPARE_WITH_TIMEOUT(m_service->notifications().size(), 1, timeoutMs);
    m_phone->clearLog();

    const QFuture<Result<void>> action =
        m_service->trigger(message.key(), QStringLiteral("Löschen"));
    const QFuture<Result<void>> unknown = m_service->dismiss(QStringLiteral("org.example.x|1|"));
    const QFuture<Result<QByteArray>> picture = m_service->fetchImage(message.key());
    QVERIFY(waitFor(action) && waitFor(unknown) && waitFor(picture));

    QCOMPARE(action.result().error().code(), ErrorCode::InvalidInput);
    QCOMPARE(unknown.result().error().code(), ErrorCode::NotFound);
    QCOMPARE(picture.result().error().code(), ErrorCode::NotFound);
    QVERIFY(m_phone->receivedCommands().isEmpty());
}

void TestNotifications::pictureIsFetchedWithinItsLimit()
{
    FakeNotification photo = chat(6, QStringLiteral("Foto"), 0);
    photo.picture = QByteArray("\xFF\xD8\xFF\xE0", 4) + QByteArray(3000, 'j');
    FakeNotification huge = chat(7, QStringLiteral("Riesig"), 0);
    huge.picture = QByteArray(int(maxNotificationImageBytes) + 100, 'h');
    m_phone->setNotifications({photo, huge});
    QVERIFY(connect());
    QTRY_COMPARE_WITH_TIMEOUT(m_service->notifications().size(), 2, timeoutMs);

    const QFuture<Result<QByteArray>> fetched = m_service->fetchImage(photo.key());
    QVERIFY(waitFor(fetched, timeoutMs));
    const QFuture<Result<QByteArray>> refused = m_service->fetchImage(huge.key());
    QVERIFY(waitFor(refused, timeoutMs));

    QVERIFY2(fetched.result().hasValue(), qPrintable(fetched.result().error().message()));
    QCOMPARE(fetched.result().value(), photo.picture);
    QVERIFY(m_phone->receivedCommands().contains(
        QStringLiteral("OBEX GET \\notificationimage\\org.example.chat\\6\\512\\512")));
    QCOMPARE(refused.result().error().code(), ErrorCode::LimitExceeded);
    QTRY_COMPARE_WITH_TIMEOUT(m_connection->state(), PhoneConnection::State::Ready, timeoutMs);
}

void TestNotifications::missingAccessIsReportedAsItsOwnError_data()
{
    QTest::addColumn<int>("phoneAccess");
    QTest::addColumn<int>("access");
    QTest::addColumn<int>("code");

    QTest::newRow("disabled") << int(FakePhoneConfig::NotificationAccess::Disabled)
                              << int(NotificationService::Access::Disabled)
                              << int(ErrorCode::PermissionDenied);
    QTest::newRow("not bound") << int(FakePhoneConfig::NotificationAccess::NotBound)
                               << int(NotificationService::Access::NotBound)
                               << int(ErrorCode::PermissionDenied);
    QTest::newRow("not supported")
        << int(FakePhoneConfig::NotificationAccess::NotSupported)
        << int(NotificationService::Access::NotSupported) << int(ErrorCode::NotSupported);
}

void TestNotifications::missingAccessIsReportedAsItsOwnError()
{
    QFETCH(int, phoneAccess);
    QFETCH(int, access);
    QFETCH(int, code);
    m_phone->config().notificationAccess = FakePhoneConfig::NotificationAccess(phoneAccess);
    m_phone->setNotifications({chat(1, QStringLiteral("Anna"), 0)});
    QVERIFY(connect());
    QTRY_COMPARE_WITH_TIMEOUT(int(m_service->access()), access, timeoutMs);

    const QFuture<Result<void>> refreshed = m_service->refresh();
    QVERIFY(waitFor(refreshed, timeoutMs));

    QCOMPARE(int(refreshed.result().error().code()), code);
    QVERIFY(m_service->notifications().isEmpty());
    QCOMPARE(m_connection->state(), PhoneConnection::State::Ready);
}

void TestNotifications::listEmptiesWhenTheConnectionEnds()
{
    m_phone->setNotifications({chat(1, QStringLiteral("Anna"), 0)});
    QVERIFY(connect());
    QTRY_COMPARE_WITH_TIMEOUT(m_service->notifications().size(), 1, timeoutMs);

    m_connection->disconnectFromPhone();

    QVERIFY(m_service->notifications().isEmpty());
    QCOMPARE(m_service->access(), NotificationService::Access::Unknown);
    const QFuture<Result<void>> refreshed = m_service->refresh();
    QVERIFY(waitFor(refreshed));
    QCOMPARE(refreshed.result().error().code(), ErrorCode::ConnectionClosed);
}

// The list kept what was read before the access ended, so the window showed
// notifications the phone no longer passed on, and hid what to do about it.
void TestNotifications::listEmptiesWhenTheAccessIsWithdrawn()
{
    m_phone->setNotifications({chat(1, QStringLiteral("Anna"), 0)});
    QVERIFY(connect());
    QTRY_COMPARE_WITH_TIMEOUT(m_service->notifications().size(), 1, timeoutMs);
    QSignalSpy posted(m_service.get(), &NotificationService::notificationPosted);
    m_phone->config().notificationAccess = FakePhoneConfig::NotificationAccess::Disabled;

    const QFuture<Result<void>> refused = m_service->refresh();
    QVERIFY(waitFor(refused, timeoutMs));

    QCOMPARE(refused.result().error().code(), ErrorCode::PermissionDenied);
    QCOMPARE(m_service->access(), NotificationService::Access::Disabled);
    QVERIFY(m_service->notifications().isEmpty());
    m_phone->config().notificationAccess = FakePhoneConfig::NotificationAccess::Granted;
    const QFuture<Result<void>> again = m_service->refresh();
    QVERIFY(waitFor(again, timeoutMs));
    QVERIFY(again.result().hasValue());
    QCOMPARE(m_service->notifications().size(), 1);
    // The list after the access came back holds nothing new for the desktop.
    QCOMPARE(posted.count(), 0);
}

QTEST_GUILESS_MAIN(TestNotifications)

#include "tst_notifications.moc"
