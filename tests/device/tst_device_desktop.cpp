#include "CommandGuard.h"

#include "core/protocol/AdbBridge.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/PhoneConnection.h"
#include "core/protocol/TcpTransport.h"
#include "core/service/CallMonitor.h"
#include "core/service/ClipboardService.h"
#include "core/service/ContactsService.h"
#include "core/service/NotificationService.h"
#include "ui/Settings.h"
#include "ui/desktop/DesktopIntegration.h"

#include "WaitFor.h"

#include <QClipboard>
#include <QDeadlineTimer>
#include <QDir>
#include <QGuiApplication>
#include <QMetaEnum>
#include <QPointer>
#include <QProcess>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <memory>
#include <optional>

using namespace qpm::core;
using namespace qpm::core::protocol;
using qpm::core::codec::PhoneNotification;
using qpm::core::service::CallMonitor;
using qpm::core::service::ClipboardService;
using qpm::core::service::ContactsService;
using qpm::core::service::NotificationService;
using qpm::test::CommandGuard;
using qpm::test::waitFor;
using qpm::ui::DesktopIntegration;
using qpm::ui::Settings;

namespace {

using State = PhoneConnection::State;
constexpr quint16 clientPort = 5210;
constexpr int readyTimeoutMs = 30000;
constexpr int stepTimeoutMs = 30000;
constexpr int roundMs = 1000;
constexpr int connectTries = 10;
// The longest a placed call goes on before the test ends it.
constexpr int callLimitMs = 10000;

QString clientPackage()
{
    return QStringLiteral("com.fjsoft.myphoneexplorer.client");
}

// The notification listener of the client, as Android lists it in its settings.
QString listenerComponent()
{
    return clientPackage() +
           QStringLiteral("/com.fjsoft.myphoneexplorer.client.NotifyListenerService");
}

bool listsListener(const QString &setting)
{
    const QStringList listeners = setting.split(u':', Qt::SkipEmptyParts);
    for (const QString &listener : listeners) {
        const QString trimmed = listener.trimmed();
        if (trimmed == listenerComponent() ||
            trimmed == clientPackage() + QStringLiteral("/.NotifyListenerService"))
            return true;
    }
    return false;
}

QString sixDigits()
{
    return QString::number(QRandomGenerator::global()->bounded(100000, 999999));
}

// Runs `step` and waits a round for `done` until `done` holds or `timeoutMs`
// passed; the result depends on that time, not on how fast the rounds run.
bool repeatUntil(const std::function<void()> &step, const std::function<bool()> &done,
                 int timeoutMs)
{
    const QDeadlineTimer deadline(timeoutMs);
    while (!done()) {
        if (deadline.hasExpired())
            return false;
        step();
        static_cast<void>(
            QTest::qWaitFor(done, int(qMin<qint64>(roundMs, deadline.remainingTime()))));
    }
    return true;
}

} // namespace

// Runs against a real client over adb, through the guard against sending and
// calling. Every target: the state of the notification access of the client
// is read, and with access the notifications are counted and checked for
// their form; nothing of their content is printed. On the emulator
// ("emulator-..." serials) the clipboard is written from this computer and
// read back through the client, a notification the shell posts arrives with
// title and text and is dismissed again, and a call the emulator console
// makes ring is reported and ended; the notification access of the client is
// switched on for that where it is off and put back afterwards. On any other
// phone, which may be someone's own, nothing is changed: no clipboard, no
// dismissing, no action of a notification, no setting. Exactly one test
// places a real call, and only on a phone that is not an emulator, with
// QPM_TEST_CALL=1, to the number in QPM_TEST_CALL_TO, which it ends after at
// most ten seconds; the number is never printed.
class TestDeviceDesktop : public QObject
{
    Q_OBJECT

public:
    explicit TestDeviceDesktop(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void notificationAccessAndListAreRead();
    void clipboardGoesBothWaysOnTheEmulator();
    void postedNotificationArrivesAndIsDismissedOnTheEmulator();
    void ringingCallIsReportedAndEndedOnTheEmulator();
    void oneRealCallIsPlacedAndEnded();

private:
    [[nodiscard]] bool connectToClient();
    [[nodiscard]] std::optional<QByteArray> adb(const QStringList &arguments) const;
    [[nodiscard]] std::optional<QString> listenerSetting() const;
    [[nodiscard]] bool allowListener();
    void restoreListener();
    [[nodiscard]] std::optional<PhoneNotification> findNotification(const QString &tag) const;
    /**
     * Sends the hang-up whatever the call monitor shows, since the phone may
     * not have reported the call, and connects once more first when the
     * connection is gone. Returns true when the phone took the hang-up.
     */
    [[nodiscard]] bool endCall();

    QString m_adbPath;
    QString m_serial;
    bool m_emulator = false;
    std::unique_ptr<AdbBridge> m_adb;
    quint16 m_localPort = 0;
    std::unique_ptr<PhoneConnection> m_connection;
    std::unique_ptr<NotificationService> m_notifications;
    std::unique_ptr<CallMonitor> m_calls;
    QPointer<CommandGuard> m_guard;
    /// The listener was allowed for the test and is taken back afterwards.
    bool m_listenerAllowed = false;
    /// A call of this test may still go on.
    bool m_callPlaced = false;
    /// The number the emulator console made ring.
    QString m_ringingNumber;
};

void TestDeviceDesktop::initTestCase()
{
    m_adbPath = qEnvironmentVariable("QPM_TEST_ADB");
    m_serial = qEnvironmentVariable("QPM_TEST_SERIAL");
    if (m_adbPath.isEmpty() || m_serial.isEmpty())
        QSKIP("QPM_TEST_ADB and QPM_TEST_SERIAL are not set");
    QVERIFY2(QDir::isAbsolutePath(m_adbPath), "QPM_TEST_ADB must be an absolute path");
    m_emulator = m_serial.startsWith(QStringLiteral("emulator-"));
    qInfo() << (m_emulator ? "Target: emulator, clipboard, notifications and a ringing call"
                           : "Target: phone, read only");

    m_adb = std::make_unique<AdbBridge>(m_adbPath);
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

void TestDeviceDesktop::cleanupTestCase()
{
    if (m_emulator && m_adb)
        restoreListener();
    if (!m_adb || m_localPort == 0)
        return;
    const QFuture<Result<void>> removed = m_adb->removeForward(m_serial, m_localPort);
    QVERIFY(waitFor(removed, readyTimeoutMs));
}

void TestDeviceDesktop::init()
{
    m_connection = std::make_unique<PhoneConnection>();
    m_notifications = std::make_unique<NotificationService>(m_connection.get());
    m_calls = std::make_unique<CallMonitor>(m_connection.get());
    QVERIFY(connectToClient());
}

void TestDeviceDesktop::cleanup()
{
    // Whatever failed above, a call of this test does not go on.
    if (std::exchange(m_callPlaced, false) && m_calls)
        QVERIFY2(endCall(), "The call of this test could not be ended");
    if (!std::exchange(m_ringingNumber, QString()).isEmpty() && m_emulator) {
        static_cast<void>(adb({QStringLiteral("emu"), QStringLiteral("gsm"),
                               QStringLiteral("cancel"), m_ringingNumber}));
    }
    if (m_guard)
        QVERIFY2(m_guard->blocked().isEmpty(), "The guard refused a command");
    m_calls.reset();
    m_notifications.reset();
    if (m_connection)
        m_connection->disconnectFromPhone();
    m_connection.reset();
}

bool TestDeviceDesktop::connectToClient()
{
    PhoneConnection::Options options;
    // On a phone that is not an emulator the test asks only for the list.
    options.notifications = m_emulator;
    m_connection->setOptions(options);
    for (int attempt = 0; attempt < connectTries; ++attempt) {
        auto guard = std::make_unique<CommandGuard>(
            std::make_unique<TcpTransport>(QStringLiteral("127.0.0.1"), m_localPort),
            m_emulator ? CommandGuard::Target::Emulator : CommandGuard::Target::Phone);
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

std::optional<QByteArray> TestDeviceDesktop::adb(const QStringList &arguments) const
{
    QProcess process;
    process.start(m_adbPath, QStringList{QStringLiteral("-s"), m_serial} + arguments);
    if (!process.waitForFinished(readyTimeoutMs) || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        return std::nullopt;
    }
    return process.readAllStandardOutput();
}

std::optional<QString> TestDeviceDesktop::listenerSetting() const
{
    const std::optional<QByteArray> value =
        adb({QStringLiteral("shell"), QStringLiteral("settings"), QStringLiteral("get"),
             QStringLiteral("secure"), QStringLiteral("enabled_notification_listeners")});
    if (!value)
        return std::nullopt;
    return QString::fromUtf8(value->trimmed());
}

bool TestDeviceDesktop::allowListener()
{
    Q_ASSERT(m_emulator);
    const std::optional<QString> before = listenerSetting();
    if (!before)
        return false;
    if (listsListener(*before))
        return true;
    if (!adb({QStringLiteral("shell"), QStringLiteral("cmd"), QStringLiteral("notification"),
              QStringLiteral("allow_listener"), listenerComponent()})) {
        return false;
    }
    m_listenerAllowed = true;
    qInfo() << "Notification access of the client switched on for the test";
    // Android writes the setting a moment after the command returns.
    return QTest::qWaitFor(
        [this]() { return listsListener(listenerSetting().value_or(QString())); }, stepTimeoutMs);
}

void TestDeviceDesktop::restoreListener()
{
    if (!std::exchange(m_listenerAllowed, false))
        return;
    QVERIFY(adb({QStringLiteral("shell"), QStringLiteral("cmd"), QStringLiteral("notification"),
                 QStringLiteral("disallow_listener"), listenerComponent()}));
    QTRY_VERIFY_WITH_TIMEOUT(!listsListener(listenerSetting().value_or(QString())), stepTimeoutMs);
}

std::optional<PhoneNotification> TestDeviceDesktop::findNotification(const QString &tag) const
{
    const QList<PhoneNotification> shown = m_notifications->notifications();
    for (const PhoneNotification &notification : shown) {
        if (notification.tag == tag)
            return notification;
    }
    return std::nullopt;
}

bool TestDeviceDesktop::endCall()
{
    if (!m_connection->session() && !connectToClient())
        return false;
    const QFuture<Result<void>> ended = m_calls->hangUp();
    if (!waitFor(ended, stepTimeoutMs))
        return false;
    if (!ended.result().hasValue())
        qInfo() << "Hanging up refused, code" << int(ended.result().error().code());
    return ended.result().hasValue();
}

void TestDeviceDesktop::notificationAccessAndListAreRead()
{
    const std::optional<QString> listeners = listenerSetting();
    QVERIFY2(listeners, "The notification access cannot be read");
    // Only whether the client's listener is on; the other listeners stay unnamed.
    qInfo() << "Notification access of the client:" << (listsListener(*listeners) ? "on" : "off");

    QTRY_VERIFY_WITH_TIMEOUT(m_notifications->access() != NotificationService::Access::Unknown,
                             stepTimeoutMs);
    const NotificationService::Access access = m_notifications->access();
    qInfo() << "Access the client reports:" << access;
    // A listener that is on can still be waiting to be bound by Android.
    if (listsListener(*listeners)) {
        QVERIFY(access == NotificationService::Access::Granted ||
                access == NotificationService::Access::NotBound);
    } else {
        QVERIFY(access == NotificationService::Access::Disabled ||
                access == NotificationService::Access::NotSupported);
    }
    const QFuture<Result<void>> refreshed = m_notifications->refresh();
    QVERIFY(waitFor(refreshed, stepTimeoutMs));
    if (access != NotificationService::Access::Granted) {
        QVERIFY(!refreshed.result().hasValue());
        QVERIFY(refreshed.result().error().code() == ErrorCode::PermissionDenied ||
                refreshed.result().error().code() == ErrorCode::NotSupported);
        return;
    }
    QVERIFY2(refreshed.result().hasValue(), qPrintable(refreshed.result().error().message()));
    const QList<PhoneNotification> shown = m_notifications->notifications();
    int withPicture = 0;
    int withActions = 0;
    int ongoing = 0;
    QSet<QString> keys;
    for (const PhoneNotification &notification : shown) {
        QVERIFY2(!keys.contains(notification.key()), "a notification came twice");
        keys.insert(notification.key());
        QVERIFY2(!notification.appLabel.isEmpty(), "a notification came without app");
        withPicture += notification.hasPicture ? 1 : 0;
        withActions += notification.actions.isEmpty() ? 0 : 1;
        ongoing += notification.ongoing ? 1 : 0;
    }
    qInfo() << "Notifications:" << shown.size() << "with picture:" << withPicture
            << "with actions:" << withActions << "ongoing:" << ongoing;
    QVERIFY(shown.size() <= codec::maxNotifications);
    QCOMPARE(m_connection->state(), State::Ready);
}

void TestDeviceDesktop::clipboardGoesBothWaysOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("The clipboard is written and read on the emulator only; the phone would show "
              "an activity for reading");
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    Settings settings(settingsDir.filePath(QStringLiteral("settings.ini")));
    settings.setClipboardSync(true);
    ClipboardService service(m_connection.get());
    DesktopIntegration integration(&settings, m_notifications.get(), &service);
    QVERIFY(service.isSyncing());

    // What the emulator held before is put back at the end, where it can be read.
    const QFuture<Result<QString>> previous = service.readFromPhone();
    QVERIFY(waitFor(previous, stepTimeoutMs));
    const std::optional<QString> saved = previous.result().hasValue()
                                             ? std::optional<QString>(previous.result().value())
                                             : std::nullopt;
    qInfo() << "Clipboard of the emulator read before:" << (saved ? "yes" : "no");

    const QString fromDesktop =
        QStringLiteral("QPM Zwischenablage ") + sixDigits() + QStringLiteral(" Grüße, 12,50 €");
    QGuiApplication::clipboard()->setText(fromDesktop);
    // The desktop clip reaches the phone; the client reads it back through its own window.
    QString onPhone;
    QVERIFY2(repeatUntil(
                 [&service, &onPhone]() {
                     const QFuture<Result<QString>> read = service.readFromPhone();
                     if (waitFor(read, stepTimeoutMs) && read.result().hasValue())
                         onPhone = read.result().value();
                 },
                 [&onPhone, &fromDesktop]() { return onPhone == fromDesktop; }, stepTimeoutMs),
             "the clip of this computer is not on the phone");

    const QString fromPhone = QStringLiteral("QPM vom Handy ") + sixDigits();
    const QFuture<Result<void>> written = service.sendToPhone(fromPhone);
    QVERIFY(waitFor(written, stepTimeoutMs));
    QVERIFY(written.result().hasValue());
    const QFuture<Result<QString>> read = service.readFromPhone();
    QVERIFY(waitFor(read, stepTimeoutMs));
    QVERIFY2(read.result().hasValue(), qPrintable(read.result().error().message()));
    QCOMPARE(read.result().value(), fromPhone);

    if (saved && !saved->isEmpty()) {
        const QFuture<Result<void>> restored = service.sendToPhone(*saved);
        QVERIFY(waitFor(restored, stepTimeoutMs));
        QVERIFY(restored.result().hasValue());
    }
    QCOMPARE(m_connection->state(), State::Ready);
}

void TestDeviceDesktop::postedNotificationArrivesAndIsDismissedOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("Notifications are posted and dismissed on the emulator only");
    QVERIFY2(allowListener(), "The notification access of the client could not be switched on");
    // The client binds its listener a moment after the access is allowed.
    QVERIFY2(
        repeatUntil(
            [this]() {
                const QFuture<Result<void>> refreshed = m_notifications->refresh();
                static_cast<void>(waitFor(refreshed, stepTimeoutMs));
            },
            [this]() { return m_notifications->access() == NotificationService::Access::Granted; },
            stepTimeoutMs),
        "The client does not read notifications");

    const QString digits = sixDigits();
    const QString tag = QStringLiteral("QPMtest") + digits;
    const QString title = QStringLiteral("QPM Titel ") + digits;
    const QString text = QStringLiteral("QPM Text ") + digits + QStringLiteral(" Grüße");
    QVERIFY(adb({QStringLiteral("shell"), QStringLiteral("cmd"), QStringLiteral("notification"),
                 QStringLiteral("post"), QStringLiteral("-t"), u'\'' + title + u'\'', tag,
                 u'\'' + text + u'\''}));

    QVERIFY2(repeatUntil(
                 [this]() {
                     const QFuture<Result<void>> refreshed = m_notifications->refresh();
                     static_cast<void>(waitFor(refreshed, stepTimeoutMs));
                 },
                 [this, &tag]() { return findNotification(tag).has_value(); }, stepTimeoutMs),
             "the posted notification is not in the list");
    const PhoneNotification posted = *findNotification(tag);
    QCOMPARE(posted.package, QStringLiteral("com.android.shell"));
    QCOMPARE(posted.title, title);
    QVERIFY(posted.text.contains(text));
    QVERIFY(!posted.ongoing);

    const QFuture<Result<void>> dismissed = m_notifications->dismiss(posted.key());
    QVERIFY(waitFor(dismissed, stepTimeoutMs));
    QVERIFY2(dismissed.result().hasValue(), qPrintable(dismissed.result().error().message()));
    QVERIFY2(repeatUntil(
                 [this]() {
                     const QFuture<Result<void>> refreshed = m_notifications->refresh();
                     static_cast<void>(waitFor(refreshed, stepTimeoutMs));
                 },
                 [this, &tag]() { return !findNotification(tag).has_value(); }, stepTimeoutMs),
             "the dismissed notification is still in the list");
    const std::optional<QByteArray> remaining =
        adb({QStringLiteral("shell"), QStringLiteral("cmd"), QStringLiteral("notification"),
             QStringLiteral("list")});
    QVERIFY(remaining);
    QVERIFY2(!remaining->contains(tag.toUtf8()), "the emulator still shows the notification");
    QCOMPARE(m_connection->state(), State::Ready);
}

void TestDeviceDesktop::ringingCallIsReportedAndEndedOnTheEmulator()
{
    if (!m_emulator)
        QSKIP("Only the emulator rings through its console");
    m_ringingNumber = QStringLiteral("4930999") + sixDigits();

    // The console only makes the emulator ring; nothing leaves the machine.
    QVERIFY(adb(
        {QStringLiteral("emu"), QStringLiteral("gsm"), QStringLiteral("call"), m_ringingNumber}));
    QTRY_COMPARE_WITH_TIMEOUT(m_calls->state(), CallMonitor::State::Ringing, stepTimeoutMs);
    qInfo() << "Ringing reported, number known:" << !m_calls->number().isEmpty();
    QVERIFY(m_calls->number().endsWith(m_ringingNumber.right(6)));

    const QFuture<Result<void>> ended = m_calls->hangUp();
    QVERIFY(waitFor(ended, stepTimeoutMs));
    qInfo() << "Hanging up answered:" << (ended.result().hasValue() ? "OK" : "refused, code")
            << int(ended.result().error().code());
    const bool endedByThePhone = QTest::qWaitFor(
        [this]() { return m_calls->state() == CallMonitor::State::Idle; }, stepTimeoutMs);
    if (!endedByThePhone) {
        QVERIFY(adb({QStringLiteral("emu"), QStringLiteral("gsm"), QStringLiteral("cancel"),
                     m_ringingNumber}));
    }
    qInfo() << "The call was ended by" << (endedByThePhone ? "ATH" : "the emulator console");
    QTRY_COMPARE_WITH_TIMEOUT(m_calls->state(), CallMonitor::State::Idle, stepTimeoutMs);
    m_ringingNumber.clear();
    QCOMPARE(m_connection->state(), State::Ready);
}

void TestDeviceDesktop::oneRealCallIsPlacedAndEnded()
{
    if (m_emulator)
        QSKIP("The emulator would hand the call to its radio; calls are tested on the fake phone");
    if (qEnvironmentVariable("QPM_TEST_CALL") != QLatin1StringView("1"))
        QSKIP("QPM_TEST_CALL is not 1, so no call is placed");
    const QString number = qEnvironmentVariable("QPM_TEST_CALL_TO");
    QVERIFY2(!number.isEmpty(), "QPM_TEST_CALL_TO is not set");
    ContactsService contacts(m_connection.get());
    QList<CallMonitor::State> seen;
    // Ends the recording with this function, also when a check above fails.
    const QObject recording;
    connect(m_calls.get(), &CallMonitor::callChanged, &recording,
            [this, &seen]() { seen.append(m_calls->state()); });

    // The call goes on for at most callLimitMs from dialling, whatever the
    // other side does and whether or not the phone reports it.
    const QDeadlineTimer callLimit(callLimitMs);
    const auto remainingMs = [&callLimit]() {
        return int(qMax<qint64>(0, callLimit.remainingTime()));
    };
    m_callPlaced = true;
    const QFuture<Result<void>> dialled = contacts.dial(number);
    QVERIFY(waitFor(dialled, remainingMs()));
    QVERIFY2(dialled.result().hasValue(),
             qPrintable(
                 QStringLiteral("not dialled, code %1").arg(int(dialled.result().error().code()))));
    const bool endedByThePhone = QTest::qWaitFor(
        [this, &seen]() { return !seen.isEmpty() && m_calls->state() == CallMonitor::State::Idle; },
        remainingMs());
    if (!endedByThePhone)
        QVERIFY2(endCall(), "The call could not be ended");
    m_callPlaced = false;
    QTRY_COMPARE_WITH_TIMEOUT(m_calls->state(), CallMonitor::State::Idle, stepTimeoutMs);
    QStringList states;
    for (const CallMonitor::State state : std::as_const(seen))
        states.append(
            QString::fromLatin1(QMetaEnum::fromType<CallMonitor::State>().valueToKey(int(state))));
    qInfo() << "Call states seen:" << states.join(QStringLiteral(", "));
    QVERIFY(seen.contains(CallMonitor::State::Dialling) ||
            seen.contains(CallMonitor::State::Active));
    QCOMPARE(seen.constLast(), CallMonitor::State::Idle);
}

QTEST_MAIN(TestDeviceDesktop)

#include "tst_device_desktop.moc"
