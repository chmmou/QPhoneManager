#include "platform/SecretServiceTypes.h"
#include "platform/SecretStore.h"

#include "WaitFor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace qpm;
using qpm::core::ErrorCode;
using qpm::core::Result;
using qpm::test::waitFor;

namespace {

QString rootPath()
{
    return QStringLiteral("/org/freedesktop/secrets");
}

QString collectionPath()
{
    return QStringLiteral("/org/freedesktop/secrets/collection/test");
}

struct FakeItem
{
    SecretServiceAttributes attributes;
    QString label;
    QByteArray value;
};

/**
 * Answers the Secret Service calls the store makes, keeps items in memory and
 * records what it received. Qt D-Bus may call handleMessage() from its own
 * thread, so every member is guarded by the mutex.
 */
class FakeSecretService : public QDBusVirtualObject
{
    Q_OBJECT

public:
    explicit FakeSecretService(QObject *parent = nullptr) : QDBusVirtualObject(parent) {}

    QString introspect(const QString &path) const override
    {
        Q_UNUSED(path)
        return {};
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        QMutexLocker locker(&m_mutex);
        const QString member = message.interface() + u'.' + message.member();
        const QList<QVariant> args = message.arguments();
        QDBusConnection bus(connection);

        if (member == QLatin1StringView("org.freedesktop.Secret.Service.OpenSession")) {
            m_algorithms.append(args.value(0).toString());
            if (args.value(0).toString() != QLatin1StringView("plain")) {
                bus.send(message.createErrorReply(
                    QStringLiteral("org.freedesktop.DBus.Error.NotSupported"), {}));
                return true;
            }
            const QString session =
                QStringLiteral("/org/freedesktop/secrets/session/%1").arg(++m_nextSession);
            m_openSessions.append(session);
            bus.send(message.createReply({QVariant::fromValue(QDBusVariant(QString())),
                                          QVariant::fromValue(QDBusObjectPath(session))}));
            return true;
        }
        if (member == QLatin1StringView("org.freedesktop.Secret.Session.Close")) {
            m_openSessions.removeAll(message.path());
            bus.send(message.createReply());
            return true;
        }
        if (member == QLatin1StringView("org.freedesktop.Secret.Service.ReadAlias")) {
            bus.send(message.createReply(QVariant::fromValue(QDBusObjectPath(collectionPath()))));
            return true;
        }
        if (member == QLatin1StringView("org.freedesktop.Secret.Service.SearchItems")) {
            const auto wanted = qdbus_cast<SecretServiceAttributes>(args.value(0));
            QList<QDBusObjectPath> found;
            for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
                if (it->attributes == wanted)
                    found.append(QDBusObjectPath(it.key()));
            }
            const QList<QDBusObjectPath> none;
            bus.send(message.createReply({QVariant::fromValue(m_locked ? none : found),
                                          QVariant::fromValue(m_locked ? found : none)}));
            return true;
        }
        if (member == QLatin1StringView("org.freedesktop.Secret.Service.Unlock")) {
            const auto objects = qdbus_cast<QList<QDBusObjectPath>>(args.value(0));
            if (!m_locked) {
                bus.send(message.createReply({QVariant::fromValue(objects),
                                              QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))}));
                return true;
            }
            const QString prompt =
                QStringLiteral("/org/freedesktop/secrets/prompt/%1").arg(++m_nextPrompt);
            m_pendingUnlocks.insert(prompt, objects);
            bus.send(message.createReply({QVariant::fromValue(QList<QDBusObjectPath>()),
                                          QVariant::fromValue(QDBusObjectPath(prompt))}));
            return true;
        }
        if (member == QLatin1StringView("org.freedesktop.Secret.Prompt.Prompt")) {
            ++m_promptsShown;
            bus.send(message.createReply());
            const QList<QDBusObjectPath> objects = m_pendingUnlocks.take(message.path());
            QDBusMessage completed = QDBusMessage::createSignal(
                message.path(), QStringLiteral("org.freedesktop.Secret.Prompt"),
                QStringLiteral("Completed"));
            if (m_dismissPrompts) {
                completed << true << QVariant::fromValue(QDBusVariant(QString()));
            } else {
                m_locked = false;
                completed << false << QVariant::fromValue(QDBusVariant(QVariant::fromValue(objects)));
            }
            bus.send(completed);
            return true;
        }
        if (member == QLatin1StringView("org.freedesktop.Secret.Service.GetSecrets")) {
            if (m_locked) {
                bus.send(message.createErrorReply(
                    QStringLiteral("org.freedesktop.Secret.Error.IsLocked"), {}));
                return true;
            }
            const auto items = qdbus_cast<QList<QDBusObjectPath>>(args.value(0));
            const auto session = args.value(1).value<QDBusObjectPath>();
            SecretServiceSecretMap secrets;
            for (const QDBusObjectPath &item : items) {
                if (m_items.contains(item.path()))
                    secrets.insert(item, {session, {}, m_items.value(item.path()).value,
                                          QStringLiteral("text/plain")});
            }
            bus.send(message.createReply(QVariant::fromValue(secrets)));
            return true;
        }
        if (member == QLatin1StringView("org.freedesktop.Secret.Collection.CreateItem")) {
            if (m_locked) {
                bus.send(message.createErrorReply(
                    QStringLiteral("org.freedesktop.Secret.Error.IsLocked"), {}));
                return true;
            }
            const auto properties = qdbus_cast<QVariantMap>(args.value(0));
            const auto secret = qdbus_cast<SecretServiceSecret>(args.value(1));
            FakeItem item;
            item.label = properties.value(QStringLiteral("org.freedesktop.Secret.Item.Label")).toString();
            item.attributes = qdbus_cast<SecretServiceAttributes>(
                properties.value(QStringLiteral("org.freedesktop.Secret.Item.Attributes")));
            item.value = secret.value;
            m_contentTypes.append(secret.contentType);
            QString path;
            if (args.value(2).toBool()) {
                for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
                    if (it->attributes == item.attributes)
                        path = it.key();
                }
            }
            if (path.isEmpty())
                path = collectionPath() + QStringLiteral("/%1").arg(++m_nextItem);
            m_items.insert(path, item);
            bus.send(message.createReply({QVariant::fromValue(QDBusObjectPath(path)),
                                          QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))}));
            return true;
        }
        if (member == QLatin1StringView("org.freedesktop.Secret.Item.Delete")) {
            m_items.remove(message.path());
            bus.send(message.createReply(QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))));
            return true;
        }
        m_unexpected.append(member);
        bus.send(message.createErrorReply(QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod"),
                                          {}));
        return true;
    }

    void setLocked(bool locked)
    {
        QMutexLocker locker(&m_mutex);
        m_locked = locked;
    }

    void setDismissPrompts(bool dismiss)
    {
        QMutexLocker locker(&m_mutex);
        m_dismissPrompts = dismiss;
    }

    void addItem(const QString &deviceId, const QByteArray &value)
    {
        QMutexLocker locker(&m_mutex);
        FakeItem item;
        item.attributes = {{QStringLiteral("application"), QStringLiteral("qphonemanager")},
                           {QStringLiteral("device"), deviceId}};
        item.value = value;
        m_items.insert(collectionPath() + QStringLiteral("/%1").arg(++m_nextItem), item);
    }

    QList<FakeItem> items() const
    {
        QMutexLocker locker(&m_mutex);
        return m_items.values();
    }

    QStringList algorithms() const
    {
        QMutexLocker locker(&m_mutex);
        return m_algorithms;
    }

    QStringList contentTypes() const
    {
        QMutexLocker locker(&m_mutex);
        return m_contentTypes;
    }

    QStringList openSessions() const
    {
        QMutexLocker locker(&m_mutex);
        return m_openSessions;
    }

    int promptsShown() const
    {
        QMutexLocker locker(&m_mutex);
        return m_promptsShown;
    }

    QStringList unexpected() const
    {
        QMutexLocker locker(&m_mutex);
        return m_unexpected;
    }

private:
    mutable QMutex m_mutex;
    QMap<QString, FakeItem> m_items;
    QMap<QString, QList<QDBusObjectPath>> m_pendingUnlocks;
    QStringList m_algorithms;
    QStringList m_contentTypes;
    QStringList m_openSessions;
    QStringList m_unexpected;
    bool m_locked = false;
    bool m_dismissPrompts = false;
    int m_promptsShown = 0;
    int m_nextSession = 0;
    int m_nextPrompt = 0;
    int m_nextItem = 0;
};

} // namespace

class TestSecretStore : public QObject
{
    Q_OBJECT

public:
    explicit TestSecretStore(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void storedPinIsReadBack();
    void storingAgainReplacesThePin();
    void phoneWithoutPinHasNone();
    void removedPinIsGone();
    void lockedCollectionIsUnlockedThroughThePrompt();
    void lockedItemIsUnlockedBeforeReading();
    void dismissedPromptStoresNothing();
    void emptyArgumentsAreRejected();
    void withoutServiceNothingIsStored();

private:
    [[nodiscard]] Result<void> store(const QString &deviceId, const QString &pin);
    [[nodiscard]] Result<std::optional<QString>> read(const QString &deviceId);

    QTemporaryDir m_busDir;
    QProcess m_daemon;
    QString m_address;
    std::unique_ptr<FakeSecretService> m_service;
    std::unique_ptr<SecretStore> m_store;
    int m_round = 0;
};

void TestSecretStore::initTestCase()
{
    QVERIFY(m_busDir.isValid());
    const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
    if (daemon.isEmpty())
        QSKIP("dbus-daemon is not installed");

    // A private bus without service directories, so that nothing on this
    // machine can be activated as the Secret Service.
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

void TestSecretStore::cleanupTestCase()
{
    if (m_daemon.state() != QProcess::NotRunning) {
        m_daemon.terminate();
        QVERIFY(m_daemon.waitForFinished());
    }
}

void TestSecretStore::init()
{
    ++m_round;
    const QString serviceName = QStringLiteral("service-%1").arg(m_round);
    const QString clientName = QStringLiteral("client-%1").arg(m_round);
    QDBusConnection serviceBus = QDBusConnection::connectToBus(m_address, serviceName);
    QDBusConnection clientBus = QDBusConnection::connectToBus(m_address, clientName);
    QVERIFY(serviceBus.isConnected());
    QVERIFY(clientBus.isConnected());

    m_service = std::make_unique<FakeSecretService>();
    QVERIFY(serviceBus.registerVirtualObject(rootPath(), m_service.get(), QDBusConnection::SubPath));
    QVERIFY(serviceBus.registerService(QString::fromLatin1(secretServiceName)));
    m_store = std::make_unique<SecretStore>(clientBus);
}

void TestSecretStore::cleanup()
{
    if (m_service)
        QVERIFY2(m_service->unexpected().isEmpty(), qPrintable(m_service->unexpected().join(u' ')));
    m_store.reset();
    QDBusConnection::disconnectFromBus(QStringLiteral("client-%1").arg(m_round));
    QDBusConnection::disconnectFromBus(QStringLiteral("service-%1").arg(m_round));
    m_service.reset();
}

Result<void> TestSecretStore::store(const QString &deviceId, const QString &pin)
{
    const QFuture<Result<void>> stored = m_store->storePin(deviceId, pin);
    if (!waitFor(stored))
        return core::Error(ErrorCode::Timeout, QStringLiteral("storePin did not finish"));
    return stored.result();
}

Result<std::optional<QString>> TestSecretStore::read(const QString &deviceId)
{
    const QFuture<Result<std::optional<QString>>> read = m_store->pin(deviceId);
    if (!waitFor(read))
        return core::Error(ErrorCode::Timeout, QStringLiteral("pin did not finish"));
    return read.result();
}

void TestSecretStore::storedPinIsReadBack()
{
    QVERIFY(store(QStringLiteral("358240051111110"), QStringLiteral("4711")).hasValue());

    const Result<std::optional<QString>> pin = read(QStringLiteral("358240051111110"));

    QVERIFY(pin.hasValue());
    QCOMPARE(pin.value().value_or(QString()), QStringLiteral("4711"));
    const QList<FakeItem> items = m_service->items();
    QCOMPARE(items.size(), 1);
    const SecretServiceAttributes expected = {
        {QStringLiteral("application"), QStringLiteral("qphonemanager")},
        {QStringLiteral("device"), QStringLiteral("358240051111110")},
    };
    QVERIFY(items.constFirst().attributes == expected);
    QVERIFY(!items.constFirst().label.isEmpty());
    QVERIFY(!items.constFirst().label.contains(QStringLiteral("4711")));
    QCOMPARE(m_service->algorithms(), (QStringList{QStringLiteral("plain"), QStringLiteral("plain")}));
    QCOMPARE(m_service->contentTypes(), QStringList{QStringLiteral("text/plain")});
    QTRY_VERIFY(m_service->openSessions().isEmpty());
}

void TestSecretStore::storingAgainReplacesThePin()
{
    QVERIFY(store(QStringLiteral("dev-1"), QStringLiteral("1111")).hasValue());
    QVERIFY(store(QStringLiteral("dev-2"), QStringLiteral("2222")).hasValue());

    QVERIFY(store(QStringLiteral("dev-1"), QStringLiteral("3333")).hasValue());

    QCOMPARE(m_service->items().size(), 2);
    QCOMPARE(read(QStringLiteral("dev-1")).value().value_or(QString()), QStringLiteral("3333"));
    QCOMPARE(read(QStringLiteral("dev-2")).value().value_or(QString()), QStringLiteral("2222"));
}

void TestSecretStore::phoneWithoutPinHasNone()
{
    QVERIFY(store(QStringLiteral("dev-1"), QStringLiteral("1111")).hasValue());

    const Result<std::optional<QString>> pin = read(QStringLiteral("dev-2"));

    QVERIFY(pin.hasValue());
    QVERIFY(!pin.value().has_value());
}

void TestSecretStore::removedPinIsGone()
{
    QVERIFY(store(QStringLiteral("dev-1"), QStringLiteral("1111")).hasValue());
    QVERIFY(store(QStringLiteral("dev-2"), QStringLiteral("2222")).hasValue());

    const QFuture<Result<void>> removed = m_store->removePin(QStringLiteral("dev-1"));
    QVERIFY(waitFor(removed));

    QVERIFY(removed.result().hasValue());
    QVERIFY(!read(QStringLiteral("dev-1")).value().has_value());
    QCOMPARE(read(QStringLiteral("dev-2")).value().value_or(QString()), QStringLiteral("2222"));
    const QFuture<Result<void>> again = m_store->removePin(QStringLiteral("dev-1"));
    QVERIFY(waitFor(again));
    QVERIFY(again.result().hasValue());
}

void TestSecretStore::lockedCollectionIsUnlockedThroughThePrompt()
{
    m_service->setLocked(true);

    QVERIFY(store(QStringLiteral("dev-1"), QStringLiteral("1111")).hasValue());

    QCOMPARE(m_service->promptsShown(), 1);
    QCOMPARE(m_service->items().size(), 1);
}

void TestSecretStore::lockedItemIsUnlockedBeforeReading()
{
    m_service->addItem(QStringLiteral("dev-1"), "5555");
    m_service->setLocked(true);

    const Result<std::optional<QString>> pin = read(QStringLiteral("dev-1"));

    QVERIFY(pin.hasValue());
    QCOMPARE(pin.value().value_or(QString()), QStringLiteral("5555"));
    QCOMPARE(m_service->promptsShown(), 1);
}

void TestSecretStore::dismissedPromptStoresNothing()
{
    m_service->setLocked(true);
    m_service->setDismissPrompts(true);

    const Result<void> stored = store(QStringLiteral("dev-1"), QStringLiteral("8642"));

    QVERIFY(!stored.hasValue());
    QCOMPARE(stored.error().code(), ErrorCode::Cancelled);
    QVERIFY(!stored.error().message().contains(QStringLiteral("8642")));
    QVERIFY(m_service->items().isEmpty());
    QTRY_VERIFY(m_service->openSessions().isEmpty());
}

void TestSecretStore::emptyArgumentsAreRejected()
{
    QCOMPARE(store(QString(), QStringLiteral("1111")).error().code(), ErrorCode::InvalidInput);
    QCOMPARE(store(QStringLiteral("dev-1"), QString()).error().code(), ErrorCode::InvalidInput);
    QCOMPARE(read(QString()).error().code(), ErrorCode::InvalidInput);
    QVERIFY(m_service->items().isEmpty());
    QVERIFY(m_service->algorithms().isEmpty());
}

void TestSecretStore::withoutServiceNothingIsStored()
{
    QDBusConnection serviceBus(QStringLiteral("service-%1").arg(m_round));
    QVERIFY(serviceBus.unregisterService(QString::fromLatin1(secretServiceName)));

    const Result<void> stored = store(QStringLiteral("dev-1"), QStringLiteral("8642"));
    const Result<std::optional<QString>> pin = read(QStringLiteral("dev-1"));

    QVERIFY(!stored.hasValue());
    QCOMPARE(stored.error().code(), ErrorCode::NotSupported);
    QVERIFY(!stored.error().message().contains(QStringLiteral("8642")));
    QVERIFY(!pin.hasValue());
    QCOMPARE(pin.error().code(), ErrorCode::NotSupported);
    QVERIFY(m_service->items().isEmpty());
}

QTEST_GUILESS_MAIN(TestSecretStore)

#include "tst_secretstore.moc"
