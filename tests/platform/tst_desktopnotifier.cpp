#include "platform/DesktopNotifier.h"

#include "WaitFor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace qpm;
using qpm::core::ErrorCode;
using qpm::core::Result;
using qpm::test::waitFor;

namespace {

const char serviceName[] = "org.freedesktop.Notifications";
const char servicePath[] = "/org/freedesktop/Notifications";
const char serviceInterface[] = "org.freedesktop.Notifications";

struct ShownNotification
{
    uint id = 0;
    uint replaces = 0;
    QString summary;
    QString body;
    QStringList actions;
};

/**
 * Answers the calls of the freedesktop notification service and records
 * them. Qt D-Bus may call handleMessage() from its own thread, so every
 * member is guarded by the mutex.
 */
class FakeNotificationService : public QDBusVirtualObject
{
    Q_OBJECT

public:
    explicit FakeNotificationService(bool bodyMarkup, QObject *parent = nullptr)
        : QDBusVirtualObject(parent), m_bodyMarkup(bodyMarkup)
    {
    }

    QString introspect(const QString &path) const override
    {
        Q_UNUSED(path)
        return {};
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        QMutexLocker locker(&m_mutex);
        QDBusConnection bus(connection);
        const QList<QVariant> args = message.arguments();
        if (message.member() == QLatin1StringView("GetCapabilities")) {
            QStringList capabilities = {QStringLiteral("actions"), QStringLiteral("body")};
            if (m_bodyMarkup)
                capabilities.append(QStringLiteral("body-markup"));
            bus.send(message.createReply(capabilities));
            return true;
        }
        if (message.member() == QLatin1StringView("Notify")) {
            ShownNotification shown;
            shown.replaces = args.value(1).toUInt();
            shown.id = shown.replaces != 0 ? shown.replaces : ++m_nextId;
            shown.summary = args.value(3).toString();
            shown.body = args.value(4).toString();
            shown.actions = args.value(5).toStringList();
            m_shown.append(shown);
            bus.send(message.createReply(shown.id));
            return true;
        }
        if (message.member() == QLatin1StringView("CloseNotification")) {
            m_closedIds.append(args.value(0).toUInt());
            bus.send(message.createReply());
            QDBusMessage signal = QDBusMessage::createSignal(QString::fromLatin1(servicePath),
                                                             QString::fromLatin1(serviceInterface),
                                                             QStringLiteral("NotificationClosed"));
            signal << args.value(0).toUInt() << 3u;
            bus.send(signal);
            return true;
        }
        m_unexpected.append(message.member());
        bus.send(message.createErrorReply(
            QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"), {}));
        return true;
    }

    QList<ShownNotification> shown() const
    {
        QMutexLocker locker(&m_mutex);
        return m_shown;
    }

    QList<uint> closedIds() const
    {
        QMutexLocker locker(&m_mutex);
        return m_closedIds;
    }

    QStringList unexpected() const
    {
        QMutexLocker locker(&m_mutex);
        return m_unexpected;
    }

private:
    mutable QMutex m_mutex;
    QList<ShownNotification> m_shown;
    QList<uint> m_closedIds;
    QStringList m_unexpected;
    bool m_bodyMarkup = false;
    uint m_nextId = 0;
};

} // namespace

// The desktop notifications against a fake notification service on a bus of
// the test's own: showing with actions, the escaping of text from the phone,
// the answers the service sends back and the case of no service.
class TestDesktopNotifier : public QObject
{
    Q_OBJECT

public:
    explicit TestDesktopNotifier(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void notificationIsShownWithItsActions();
    void bodyIsEscapedOnlyWhereTheServiceReadsMarkup_data();
    void bodyIsEscapedOnlyWhereTheServiceReadsMarkup();
    void chosenActionAndClosingAreReported();
    void replacingKeepsTheId();
    void withoutServiceNothingIsShown();

private:
    [[nodiscard]] bool startService(bool bodyMarkup);
    [[nodiscard]] Result<uint> show(const QString &summary, const QString &body,
                                    const QList<DesktopNotifier::Action> &actions,
                                    uint replaces = 0);

    QTemporaryDir m_busDir;
    QProcess m_daemon;
    QString m_address;
    std::unique_ptr<FakeNotificationService> m_service;
    std::unique_ptr<DesktopNotifier> m_notifier;
    int m_round = 0;
};

void TestDesktopNotifier::initTestCase()
{
    QVERIFY(m_busDir.isValid());
    const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
    if (daemon.isEmpty())
        QSKIP("dbus-daemon is not installed");

    // A private bus without service directories, so that nothing on this
    // machine is activated as the notification service.
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
    m_address = QString::fromUtf8(m_daemon.readLine()).trimmed();
    QVERIFY(!m_address.isEmpty());
}

void TestDesktopNotifier::cleanupTestCase()
{
    if (m_daemon.state() != QProcess::NotRunning) {
        m_daemon.terminate();
        QVERIFY(m_daemon.waitForFinished());
    }
}

bool TestDesktopNotifier::startService(bool bodyMarkup)
{
    ++m_round;
    QDBusConnection serviceBus =
        QDBusConnection::connectToBus(m_address, QStringLiteral("service-%1").arg(m_round));
    QDBusConnection clientBus =
        QDBusConnection::connectToBus(m_address, QStringLiteral("client-%1").arg(m_round));
    if (!serviceBus.isConnected() || !clientBus.isConnected())
        return false;
    m_service = std::make_unique<FakeNotificationService>(bodyMarkup);
    if (!serviceBus.registerVirtualObject(QString::fromLatin1(servicePath), m_service.get()) ||
        !serviceBus.registerService(QString::fromLatin1(serviceName))) {
        return false;
    }
    m_notifier = std::make_unique<DesktopNotifier>(clientBus);
    return true;
}

void TestDesktopNotifier::cleanup()
{
    if (m_service)
        QVERIFY2(m_service->unexpected().isEmpty(), qPrintable(m_service->unexpected().join(u' ')));
    m_notifier.reset();
    QDBusConnection::disconnectFromBus(QStringLiteral("client-%1").arg(m_round));
    QDBusConnection::disconnectFromBus(QStringLiteral("service-%1").arg(m_round));
    m_service.reset();
}

Result<uint> TestDesktopNotifier::show(const QString &summary, const QString &body,
                                       const QList<DesktopNotifier::Action> &actions, uint replaces)
{
    const QFuture<Result<uint>> shown = m_notifier->show(summary, body, actions, replaces);
    if (!waitFor(shown))
        return core::Error(ErrorCode::Timeout, QStringLiteral("show did not finish"));
    return shown.result();
}

void TestDesktopNotifier::notificationIsShownWithItsActions()
{
    QVERIFY(startService(true));

    const Result<uint> id = show(QStringLiteral("Anna Becker"), QStringLiteral("Bis später"),
                                 {{QStringLiteral("default"), QStringLiteral("Öffnen")},
                                  {QStringLiteral("action-0"), QStringLiteral("Antworten")},
                                  {QString(), QStringLiteral("ohne Schlüssel")},
                                  {QStringLiteral("dismiss"), QStringLiteral("Schließen")}});

    QVERIFY2(id.hasValue(), qPrintable(id.error().message()));
    const QList<ShownNotification> shown = m_service->shown();
    QCOMPARE(shown.size(), 1);
    QCOMPARE(shown.constFirst().id, id.value());
    QCOMPARE(shown.constFirst().summary, QStringLiteral("Anna Becker"));
    QCOMPARE(shown.constFirst().body, QStringLiteral("Bis später"));
    QCOMPARE(shown.constFirst().actions,
             (QStringList{QStringLiteral("default"), QStringLiteral("Öffnen"),
                          QStringLiteral("action-0"), QStringLiteral("Antworten"),
                          QStringLiteral("dismiss"), QStringLiteral("Schließen")}));
}

void TestDesktopNotifier::bodyIsEscapedOnlyWhereTheServiceReadsMarkup_data()
{
    QTest::addColumn<bool>("markup");
    QTest::addColumn<QString>("expected");

    QTest::newRow("markup") << true << QStringLiteral("&lt;b&gt;fett&lt;/b&gt; &amp; mehr");
    QTest::newRow("plain") << false << QStringLiteral("<b>fett</b> & mehr");
}

void TestDesktopNotifier::bodyIsEscapedOnlyWhereTheServiceReadsMarkup()
{
    QFETCH(bool, markup);
    QFETCH(QString, expected);
    QVERIFY(startService(markup));

    QVERIFY(
        show(QStringLiteral("<i>Titel</i>"), QStringLiteral("<b>fett</b> & mehr"), {}).hasValue());
    QVERIFY(show(QStringLiteral("Zweite"), QStringLiteral("<b>fett</b> & mehr"), {}).hasValue());

    const QList<ShownNotification> shown = m_service->shown();
    QCOMPARE(shown.size(), 2);
    QCOMPARE(shown.at(0).body, expected);
    QCOMPARE(shown.at(1).body, expected);
    QCOMPARE(shown.at(0).summary, QStringLiteral("<i>Titel</i>"));
}

void TestDesktopNotifier::chosenActionAndClosingAreReported()
{
    QVERIFY(startService(true));
    QSignalSpy actions(m_notifier.get(), &DesktopNotifier::actionInvoked);
    QSignalSpy closed(m_notifier.get(), &DesktopNotifier::closed);
    const Result<uint> id = show(QStringLiteral("Anna"), QStringLiteral("Hallo"),
                                 {{QStringLiteral("action-0"), QStringLiteral("Antworten")}});
    QVERIFY(id.hasValue());

    QDBusConnection serviceBus(QStringLiteral("service-%1").arg(m_round));
    QDBusMessage invoked = QDBusMessage::createSignal(QString::fromLatin1(servicePath),
                                                      QString::fromLatin1(serviceInterface),
                                                      QStringLiteral("ActionInvoked"));
    invoked << id.value() << QStringLiteral("action-0");
    QVERIFY(serviceBus.send(invoked));
    QTRY_COMPARE(actions.count(), 1);
    const QFuture<Result<void>> closing = m_notifier->close(id.value());
    QVERIFY(waitFor(closing));

    QCOMPARE(actions.first().at(0).toUInt(), id.value());
    QCOMPARE(actions.first().at(1).toString(), QStringLiteral("action-0"));
    QVERIFY(closing.result().hasValue());
    QCOMPARE(m_service->closedIds(), QList<uint>{id.value()});
    QTRY_COMPARE(closed.count(), 1);
    QCOMPARE(closed.first().at(0).toUInt(), id.value());
    QCOMPARE(closed.first().at(1).toUInt(), 3u);
}

void TestDesktopNotifier::replacingKeepsTheId()
{
    QVERIFY(startService(true));
    const Result<uint> first = show(QStringLiteral("Anruf"), QStringLiteral("klingelt"), {});
    QVERIFY(first.hasValue());

    const Result<uint> second =
        show(QStringLiteral("Anruf"), QStringLiteral("verpasst"), {}, first.value());

    QVERIFY(second.hasValue());
    QCOMPARE(second.value(), first.value());
    QCOMPARE(m_service->shown().constLast().replaces, first.value());
}

void TestDesktopNotifier::withoutServiceNothingIsShown()
{
    QVERIFY(startService(true));
    QDBusConnection serviceBus(QStringLiteral("service-%1").arg(m_round));
    QVERIFY(serviceBus.unregisterService(QString::fromLatin1(serviceName)));

    const Result<uint> id = show(QStringLiteral("Anna"), QStringLiteral("Hallo"), {});
    const QFuture<Result<void>> closing = m_notifier->close(7);
    QVERIFY(waitFor(closing));

    QVERIFY(!id.hasValue());
    QCOMPARE(id.error().code(), ErrorCode::NotSupported);
    QCOMPARE(closing.result().error().code(), ErrorCode::NotSupported);
    QVERIFY(m_service->shown().isEmpty());
}

QTEST_GUILESS_MAIN(TestDesktopNotifier)

#include "tst_desktopnotifier.moc"
