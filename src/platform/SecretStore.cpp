#include "platform/SecretStore.h"

#include "core/Logging.h"
#include "platform/SecretServiceTypes.h"

#include <QDBusError>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QPointer>
#include <QPromise>

#include <memory>
#include <utility>

namespace qpm {

using core::Error;
using core::ErrorCode;
using core::Result;

namespace {

const char servicePath[] = "/org/freedesktop/secrets";
const char serviceInterface[] = "org.freedesktop.Secret.Service";
const char collectionInterface[] = "org.freedesktop.Secret.Collection";
const char itemInterface[] = "org.freedesktop.Secret.Item";
const char sessionInterface[] = "org.freedesktop.Secret.Session";
const char promptInterface[] = "org.freedesktop.Secret.Prompt";
const char defaultCollectionAlias[] = "default";
const char noPrompt[] = "/";

SecretServiceAttributes attributesFor(const QString &deviceId)
{
    return {
        {QStringLiteral("application"), QStringLiteral("qphonemanager")},
        {QStringLiteral("device"), deviceId},
    };
}

Error replyError(const QDBusMessage &reply, const QString &method)
{
    const QString name = reply.errorName();
    if (name == QLatin1StringView("org.freedesktop.DBus.Error.ServiceUnknown") ||
        name == QLatin1StringView("org.freedesktop.DBus.Error.NameHasNoOwner") ||
        name == QLatin1StringView("org.freedesktop.DBus.Error.Disconnected")) {
        return Error(ErrorCode::NotSupported, QStringLiteral("The Secret Service is not available"));
    }
    // Only the error name: the message text comes from another process and is
    // not shown or logged unchecked.
    return Error(ErrorCode::IoFailure,
                 QStringLiteral("Secret Service call %1 failed: %2").arg(method, name));
}

Error malformedReply(const QString &method)
{
    return Error(ErrorCode::ProtocolError,
                 QStringLiteral("Unexpected answer of the Secret Service to %1").arg(method));
}

template <typename T> void finish(const std::shared_ptr<QPromise<T>> &promise, const T &value)
{
    promise->addResult(value);
    promise->finish();
}

template <typename T> std::shared_ptr<QPromise<T>> startPromise()
{
    auto promise = std::make_shared<QPromise<T>>();
    promise->start();
    return promise;
}

/**
 * Waits for the Completed signal of one prompt and deletes itself once it
 * arrived or was abandoned.
 */
class PromptWatcher : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(bool dismissed, const QDBusVariant &result)>;

    PromptWatcher(const QDBusConnection &bus, QString path, Done done, QObject *parent = nullptr)
        : QObject(parent), m_bus(bus), m_path(std::move(path)), m_done(std::move(done))
    {
    }

    /// Subscribes to the signal; returns false when the bus refuses.
    [[nodiscard]] bool start()
    {
        // Qt D-Bus offers no pointer-to-member form for bus signals, so the
        // slot is named by its signature.
        return m_bus.connect(QString::fromLatin1(secretServiceName), m_path,
                             QString::fromLatin1(promptInterface), QStringLiteral("Completed"), this,
                             SLOT(completed(bool,QDBusVariant)));
    }

    /// Stops waiting without calling the handler.
    void abandon()
    {
        stop();
        deleteLater();
    }

private Q_SLOTS:
    void completed(bool dismissed, const QDBusVariant &result)
    {
        stop();
        deleteLater();
        if (m_done)
            std::exchange(m_done, nullptr)(dismissed, result);
    }

private:
    void stop()
    {
        m_bus.disconnect(QString::fromLatin1(secretServiceName), m_path,
                         QString::fromLatin1(promptInterface), QStringLiteral("Completed"), this,
                         SLOT(completed(bool,QDBusVariant)));
    }

    QDBusConnection m_bus;
    QString m_path;
    Done m_done;
};

} // namespace

SecretStore::SecretStore(QObject *parent) : SecretStore(QDBusConnection::sessionBus(), parent) {}

SecretStore::SecretStore(const QDBusConnection &bus, QObject *parent) : QObject(parent), m_bus(bus)
{
    registerSecretServiceTypes();
}

QFuture<Result<void>> SecretStore::storePin(const QString &deviceId, const QString &pin)
{
    auto promise = startPromise<Result<void>>();
    QFuture<Result<void>> future = promise->future();
    if (deviceId.isEmpty() || pin.isEmpty()) {
        finish(promise, Result<void>(Error(ErrorCode::InvalidInput,
                                           QStringLiteral("Device id and PIN must not be empty"))));
        return future;
    }

    openSession([this, promise, deviceId, pin](const Result<QDBusObjectPath> &session) {
        if (!session.hasValue()) {
            finish(promise, Result<void>(session.error()));
            return;
        }
        const QDBusObjectPath sessionPath = session.value();
        const auto done = [this, promise, sessionPath](const Result<void> &result) {
            closeSession(sessionPath);
            finish(promise, result);
        };

        call(QString::fromLatin1(servicePath), QString::fromLatin1(serviceInterface),
             QStringLiteral("ReadAlias"), {QString::fromLatin1(defaultCollectionAlias)},
             [this, done, sessionPath, deviceId, pin](const QDBusMessage &reply) {
                 if (reply.type() == QDBusMessage::ErrorMessage) {
                     done(replyError(reply, QStringLiteral("ReadAlias")));
                     return;
                 }
                 const QList<QVariant> args = reply.arguments();
                 if (args.size() != 1 || !args.at(0).canConvert<QDBusObjectPath>()) {
                     done(malformedReply(QStringLiteral("ReadAlias")));
                     return;
                 }
                 const QDBusObjectPath collection = args.at(0).value<QDBusObjectPath>();
                 if (collection.path() == QLatin1StringView(noPrompt)) {
                     done(Error(ErrorCode::NotFound,
                                QStringLiteral("The Secret Service has no default collection")));
                     return;
                 }

                 unlock({collection}, [this, done, collection, sessionPath, deviceId,
                                       pin](const Result<QList<QDBusObjectPath>> &unlocked) {
                     if (!unlocked.hasValue()) {
                         done(unlocked.error());
                         return;
                     }
                     const QVariantMap properties = {
                         {QStringLiteral("org.freedesktop.Secret.Item.Label"),
                          tr("QPhoneManager: PIN of a phone")},
                         {QStringLiteral("org.freedesktop.Secret.Item.Attributes"),
                          QVariant::fromValue(attributesFor(deviceId))},
                     };
                     const SecretServiceSecret secret{sessionPath, {}, pin.toUtf8(),
                                                      QStringLiteral("text/plain")};
                     call(collection.path(), QString::fromLatin1(collectionInterface),
                          QStringLiteral("CreateItem"),
                          {properties, QVariant::fromValue(secret), true},
                          [this, done](const QDBusMessage &created) {
                              if (created.type() == QDBusMessage::ErrorMessage) {
                                  done(replyError(created, QStringLiteral("CreateItem")));
                                  return;
                              }
                              const QList<QVariant> args = created.arguments();
                              if (args.size() != 2 || !args.at(1).canConvert<QDBusObjectPath>()) {
                                  done(malformedReply(QStringLiteral("CreateItem")));
                                  return;
                              }
                              const QDBusObjectPath prompt = args.at(1).value<QDBusObjectPath>();
                              if (prompt.path() == QLatin1StringView(noPrompt)) {
                                  done({});
                                  return;
                              }
                              runPrompt(prompt, [done](const Result<QDBusVariant> &result) {
                                  done(result.hasValue() ? Result<void>() : result.error());
                              });
                          });
                 });
             });
    });
    return future;
}

QFuture<Result<std::optional<QString>>> SecretStore::pin(const QString &deviceId)
{
    using PinResult = Result<std::optional<QString>>;
    auto promise = startPromise<PinResult>();
    QFuture<PinResult> future = promise->future();
    if (deviceId.isEmpty()) {
        finish(promise,
               PinResult(Error(ErrorCode::InvalidInput, QStringLiteral("The device id is empty"))));
        return future;
    }

    openSession([this, promise, deviceId](const Result<QDBusObjectPath> &session) {
        if (!session.hasValue()) {
            finish(promise, PinResult(session.error()));
            return;
        }
        const QDBusObjectPath sessionPath = session.value();
        const auto done = [this, promise, sessionPath](const PinResult &result) {
            closeSession(sessionPath);
            finish(promise, result);
        };

        findItems(deviceId, [this, done, sessionPath](const Result<QList<QDBusObjectPath>> &items) {
            if (!items.hasValue()) {
                done(items.error());
                return;
            }
            if (items.value().isEmpty()) {
                done(std::optional<QString>());
                return;
            }
            call(QString::fromLatin1(servicePath), QString::fromLatin1(serviceInterface),
                 QStringLiteral("GetSecrets"),
                 {QVariant::fromValue(items.value()), QVariant::fromValue(sessionPath)},
                 [done, first = items.value().constFirst()](const QDBusMessage &reply) {
                     if (reply.type() == QDBusMessage::ErrorMessage) {
                         done(replyError(reply, QStringLiteral("GetSecrets")));
                         return;
                     }
                     const QList<QVariant> args = reply.arguments();
                     if (args.size() != 1 || !args.at(0).canConvert<QDBusArgument>()) {
                         done(malformedReply(QStringLiteral("GetSecrets")));
                         return;
                     }
                     const auto secrets =
                         qdbus_cast<SecretServiceSecretMap>(args.at(0).value<QDBusArgument>());
                     const auto found = secrets.constFind(first);
                     if (found == secrets.cend()) {
                         done(malformedReply(QStringLiteral("GetSecrets")));
                         return;
                     }
                     done(std::optional<QString>(QString::fromUtf8(found->value)));
                 });
        });
    });
    return future;
}

QFuture<Result<void>> SecretStore::removePin(const QString &deviceId)
{
    auto promise = startPromise<Result<void>>();
    QFuture<Result<void>> future = promise->future();
    if (deviceId.isEmpty()) {
        finish(promise, Result<void>(Error(ErrorCode::InvalidInput,
                                           QStringLiteral("The device id is empty"))));
        return future;
    }

    findItems(deviceId, [this, promise](const Result<QList<QDBusObjectPath>> &items) {
        if (!items.hasValue()) {
            finish(promise, Result<void>(items.error()));
            return;
        }
        deleteItems(items.value(),
                    [promise](const Result<void> &result) { finish(promise, result); });
    });
    return future;
}

void SecretStore::call(const QString &path, const QString &interface, const QString &method,
                       const QList<QVariant> &arguments, const ReplyHandler &handler)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QString::fromLatin1(secretServiceName),
                                                          path, interface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, handler]() {
        watcher->deleteLater();
        handler(watcher->reply());
    });
}

void SecretStore::openSession(const Continuation<QDBusObjectPath> &next)
{
    call(QString::fromLatin1(servicePath), QString::fromLatin1(serviceInterface),
         QStringLiteral("OpenSession"),
         {QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString()))},
         [next](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage) {
                 next(replyError(reply, QStringLiteral("OpenSession")));
                 return;
             }
             const QList<QVariant> args = reply.arguments();
             if (args.size() != 2 || !args.at(1).canConvert<QDBusObjectPath>()) {
                 next(malformedReply(QStringLiteral("OpenSession")));
                 return;
             }
             next(args.at(1).value<QDBusObjectPath>());
         });
}

void SecretStore::closeSession(const QDBusObjectPath &session)
{
    call(session.path(), QString::fromLatin1(sessionInterface), QStringLiteral("Close"), {},
         [](const QDBusMessage &reply) {
             // The service drops the session with the connection anyway, so a
             // failed close needs no further handling.
             if (reply.type() == QDBusMessage::ErrorMessage)
                 qCWarning(core::lcPlatform) << "Closing a Secret Service session failed:"
                                             << reply.errorName();
         });
}

void SecretStore::findItems(const QString &deviceId,
                            const Continuation<QList<QDBusObjectPath>> &next)
{
    call(QString::fromLatin1(servicePath), QString::fromLatin1(serviceInterface),
         QStringLiteral("SearchItems"), {QVariant::fromValue(attributesFor(deviceId))},
         [this, next](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage) {
                 next(replyError(reply, QStringLiteral("SearchItems")));
                 return;
             }
             const QList<QVariant> args = reply.arguments();
             if (args.size() != 2 || !args.at(0).canConvert<QDBusArgument>() ||
                 !args.at(1).canConvert<QDBusArgument>()) {
                 next(malformedReply(QStringLiteral("SearchItems")));
                 return;
             }
             const auto unlocked = qdbus_cast<QList<QDBusObjectPath>>(args.at(0));
             const auto locked = qdbus_cast<QList<QDBusObjectPath>>(args.at(1));
             if (locked.isEmpty()) {
                 next(unlocked);
                 return;
             }
             unlock(locked, [next, unlocked](const Result<QList<QDBusObjectPath>> &result) {
                 if (!result.hasValue()) {
                     next(result);
                     return;
                 }
                 next(unlocked + result.value());
             });
         });
}

void SecretStore::unlock(const QList<QDBusObjectPath> &objects,
                         const Continuation<QList<QDBusObjectPath>> &next)
{
    call(QString::fromLatin1(servicePath), QString::fromLatin1(serviceInterface),
         QStringLiteral("Unlock"), {QVariant::fromValue(objects)},
         [this, next](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage) {
                 next(replyError(reply, QStringLiteral("Unlock")));
                 return;
             }
             const QList<QVariant> args = reply.arguments();
             if (args.size() != 2 || !args.at(0).canConvert<QDBusArgument>() ||
                 !args.at(1).canConvert<QDBusObjectPath>()) {
                 next(malformedReply(QStringLiteral("Unlock")));
                 return;
             }
             const QDBusObjectPath prompt = args.at(1).value<QDBusObjectPath>();
             if (prompt.path() == QLatin1StringView(noPrompt)) {
                 next(qdbus_cast<QList<QDBusObjectPath>>(args.at(0)));
                 return;
             }
             runPrompt(prompt, [next](const Result<QDBusVariant> &result) {
                 if (!result.hasValue()) {
                     next(result.error());
                     return;
                 }
                 const QVariant value = result.value().variant();
                 if (!value.canConvert<QDBusArgument>()) {
                     next(malformedReply(QStringLiteral("Unlock")));
                     return;
                 }
                 next(qdbus_cast<QList<QDBusObjectPath>>(value));
             });
         });
}

void SecretStore::runPrompt(const QDBusObjectPath &prompt, const Continuation<QDBusVariant> &next)
{
    auto *watcher = new PromptWatcher(
        m_bus, prompt.path(),
        [next](bool dismissed, const QDBusVariant &result) {
            if (dismissed) {
                next(Error(ErrorCode::Cancelled, QStringLiteral("The unlock prompt was dismissed")));
                return;
            }
            next(result);
        },
        this);
    if (!watcher->start()) {
        watcher->abandon();
        next(Error(ErrorCode::IoFailure, QStringLiteral("Cannot watch the Secret Service prompt")));
        return;
    }

    call(prompt.path(), QString::fromLatin1(promptInterface), QStringLiteral("Prompt"), {QString()},
         [next, guard = QPointer<PromptWatcher>(watcher)](const QDBusMessage &reply) {
             if (reply.type() != QDBusMessage::ErrorMessage || guard.isNull())
                 return;
             guard->abandon();
             next(replyError(reply, QStringLiteral("Prompt")));
         });
}

void SecretStore::deleteItems(QList<QDBusObjectPath> items, const Continuation<void> &next)
{
    if (items.isEmpty()) {
        next({});
        return;
    }
    const QDBusObjectPath item = items.takeFirst();
    call(item.path(), QString::fromLatin1(itemInterface), QStringLiteral("Delete"), {},
         [this, next, items](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage) {
                 next(replyError(reply, QStringLiteral("Delete")));
                 return;
             }
             const QList<QVariant> args = reply.arguments();
             if (args.size() != 1 || !args.at(0).canConvert<QDBusObjectPath>()) {
                 next(malformedReply(QStringLiteral("Delete")));
                 return;
             }
             const QDBusObjectPath prompt = args.at(0).value<QDBusObjectPath>();
             if (prompt.path() == QLatin1StringView(noPrompt)) {
                 deleteItems(items, next);
                 return;
             }
             runPrompt(prompt, [this, next, items](const Result<QDBusVariant> &result) {
                 if (!result.hasValue()) {
                     next(result.error());
                     return;
                 }
                 deleteItems(items, next);
             });
         });
}

} // namespace qpm

#include "SecretStore.moc"
