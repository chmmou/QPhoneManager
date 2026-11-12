#include "core/service/AppsService.h"

#include "core/Logging.h"
#include "core/codec/FolderListing.h"
#include "core/protocol/AtSession.h"
#include "core/protocol/ObexClient.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/FileService.h"

#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QPromise>
#include <QtConcurrentRun>

#include <chrono>
#include <functional>
#include <memory>

namespace qpm::core::service {

namespace {

using codec::AppInfo;
using protocol::AtReply;
using protocol::ObexClient;
using protocol::ObexResponse;

constexpr std::chrono::seconds listTimeout{90};
constexpr std::chrono::seconds iconTimeout{20};
constexpr QByteArrayView zipSignature("PK\x03\x04", 4);

template <typename T> QFuture<Result<T>> failed(const Error &error)
{
    return QtFuture::makeReadyValueFuture(Result<T>(error));
}

Error notConnected()
{
    return Error(ErrorCode::ConnectionClosed, QStringLiteral("The phone is not connected"));
}

Error unusablePackage()
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("Unusable package name"));
}

// `what` names the request without its arguments.
Error replyError(const QFuture<AtReply> &future, const QString &what)
{
    if (future.isCanceled() || future.resultCount() == 0)
        return notConnected();
    const AtReply reply = future.result();
    switch (reply.status) {
    case AtReply::Status::Ok:
        return {};
    case AtReply::Status::Timeout:
        return Error(ErrorCode::Timeout, QStringLiteral("The phone did not answer %1").arg(what));
    case AtReply::Status::Closed:
    case AtReply::Status::AccessDenied:
        return notConnected();
    case AtReply::Status::PermissionDenied:
        return Error(ErrorCode::PermissionDenied,
                     QStringLiteral("The phone may not do %1").arg(what));
    case AtReply::Status::NotSupported:
        return Error(ErrorCode::NotSupported,
                     QStringLiteral("The phone does not support %1").arg(what));
    case AtReply::Status::Dump:
        // The client passes on the exception of Android; an app it does not
        // know arrives as a NameNotFoundException.
        if (reply.detail.contains(QLatin1StringView("NameNotFoundException")))
            return Error(ErrorCode::NotFound, QStringLiteral("The phone does not know the app"));
        break;
    case AtReply::Status::CmsError:
    case AtReply::Status::Error:
        break;
    }
    return Error(ErrorCode::Rejected, QStringLiteral("The phone refused %1").arg(what));
}

QFuture<Result<void>> sendSimple(protocol::PhoneConnection *connection, QObject *context,
                                 const Result<QString> &command, const QString &what)
{
    if (!command.hasValue())
        return failed<void>(command.error());
    protocol::AtSession *session = connection->session();
    if (!session)
        return failed<void>(notConnected());
    QFuture<AtReply> reply = session->send(command.value());
    return reply
        .then(context,
              [reply, what](const AtReply &) -> Result<void> {
                  return Result<void>(replyError(reply, what));
              })
        .onCanceled(context, []() { return Result<void>(notConnected()); });
}

// Errors after which further requests in the same session cannot succeed.
bool endsTheSession(const Error &error)
{
    return error.code() == ErrorCode::ConnectionClosed || error.code() == ErrorCode::Timeout ||
           error.code() == ErrorCode::Cancelled;
}

void fetchNextIcon(QPointer<ObexClient> obex, QObject *context, QStringList packages,
                   std::shared_ptr<QList<AppIcon>> icons,
                   std::function<void(const Result<QList<AppIcon>> &)> done)
{
    if (packages.isEmpty()) {
        done(*icons);
        return;
    }
    if (!obex) {
        done(Error(ErrorCode::ConnectionClosed, QStringLiteral("The OBEX session ended")));
        return;
    }
    const QString package = packages.takeFirst();
    const Result<QString> name = codec::appIconName(package, AppsService::iconSize);
    if (!name.hasValue()) {
        fetchNextIcon(obex, context, packages, icons, done);
        return;
    }
    obex->get(name.value(), {}, iconTimeout, codec::maxAppIconBytes)
        .then(context,
              [obex, context, packages, icons, done, package](const Result<ObexResponse> &answer) {
                  if (answer.hasValue() && !answer.value().body.isEmpty()) {
                      icons->append({package, answer.value().body});
                  } else if (!answer.hasValue() && endsTheSession(answer.error())) {
                      done(answer.error());
                      return;
                  }
                  fetchNextIcon(obex, context, packages, icons, done);
              });
}

struct ApkCheck
{
    Error error;
    QString name;
};

// Reads only the start of the file; the type follows from the content, not
// from the name.
ApkCheck checkApk(const QString &localPath)
{
    const QFileInfo info(localPath);
    const QString name = info.fileName();
    if (!codec::isUsableFileName(name) ||
        !name.endsWith(QLatin1StringView(".apk"), Qt::CaseInsensitive)) {
        return {Error(ErrorCode::InvalidInput, QStringLiteral("The file name is no APK name")), {}};
    }
    QFile file(localPath);
    if (!info.isFile() || !file.open(QIODevice::ReadOnly))
        return {Error(ErrorCode::IoFailure, QStringLiteral("The local file cannot be read")), {}};
    if (file.size() <= 0 || quint64(file.size()) > codec::maxApkBytes)
        return {Error(ErrorCode::InvalidInput, QStringLiteral("The APK is empty or too large")),
                {}};
    if (file.read(zipSignature.size()) != zipSignature)
        return {Error(ErrorCode::InvalidInput, QStringLiteral("The file is no APK")), {}};
    return {{}, name};
}

} // namespace

AppsService::AppsService(protocol::PhoneConnection *connection, FileService *files, QObject *parent)
    : QObject(parent), m_connection(connection), m_files(files)
{
    Q_ASSERT(connection && files);
    connect(connection, &protocol::PhoneConnection::unsolicited, this, &AppsService::onUnsolicited);
}

QFuture<Result<QList<AppInfo>>> AppsService::list()
{
    protocol::AtSession *session = m_connection->session();
    if (!session)
        return failed<QList<AppInfo>>(notConnected());
    QFuture<AtReply> reply = session->send(codec::listAppsCommand(), listTimeout);
    return reply
        .then(this,
              [reply](const AtReply &answer) -> QFuture<Result<QList<AppInfo>>> {
                  const Error error = replyError(reply, QStringLiteral("the app list"));
                  if (error.isError())
                      return failed<QList<AppInfo>>(error);
                  return QtConcurrent::run([lines = answer.lines]() -> Result<QList<AppInfo>> {
                      const Result<codec::AppList> parsed = codec::parseAppList(lines);
                      if (!parsed.hasValue())
                          return parsed.error();
                      if (parsed.value().malformed > 0) {
                          qCWarning(lcProtocol) << "App list lines left out, not readable:"
                                                << parsed.value().malformed;
                      }
                      return parsed.value().apps;
                  });
              })
        .unwrap()
        .onCanceled(this, []() { return Result<QList<AppInfo>>(notConnected()); });
}

QFuture<Result<QList<AppIcon>>> AppsService::fetchIcons(const QStringList &packages)
{
    if (packages.size() > maxIconsPerRequest) {
        return failed<QList<AppIcon>>(
            Error(ErrorCode::LimitExceeded, QStringLiteral("Too many icons in one request")));
    }
    for (const QString &package : packages) {
        if (!codec::isValidPackageName(package))
            return failed<QList<AppIcon>>(unusablePackage());
    }
    if (!m_connection->session())
        return failed<QList<AppIcon>>(notConnected());
    if (packages.isEmpty())
        return QtFuture::makeReadyValueFuture(Result<QList<AppIcon>>(QList<AppIcon>()));
    return m_connection->withObex<QList<AppIcon>>(
        ObexClient::Target::FileBrowser, [this, packages](ObexClient &obex) {
            auto promise = std::make_shared<QPromise<Result<QList<AppIcon>>>>();
            promise->start();
            fetchNextIcon(&obex, this, packages, std::make_shared<QList<AppIcon>>(),
                          [promise](const Result<QList<AppIcon>> &result) {
                              promise->addResult(result);
                              promise->finish();
                          });
            return promise->future();
        });
}

QFuture<Result<void>> AppsService::start(const QString &package)
{
    return sendSimple(m_connection, this, codec::startAppCommand(package),
                      QStringLiteral("to start the app"));
}

QFuture<Result<void>> AppsService::requestUninstall(const QString &package)
{
    return sendSimple(m_connection, this, codec::uninstallAppCommand(package),
                      QStringLiteral("to ask for the removal"));
}

QFuture<Result<void>> AppsService::backup(const AppInfo &app, const QString &localPath)
{
    if (!codec::isValidPackageName(app.package))
        return failed<void>(unusablePackage());
    if (app.sourcePath.isEmpty() || !app.size) {
        return failed<void>(
            Error(ErrorCode::NotSupported, QStringLiteral("The phone gave no APK for the app")));
    }
    const Result<QString> path = codec::apkPhonePath(app.sourcePath);
    if (!path.hasValue())
        return failed<void>(path.error());
    return m_files->download(RemoteFile{path.value(), *app.size, app.installed}, localPath);
}

QFuture<Result<void>> AppsService::install(const QString &localPath)
{
    const ApkCheck checked = checkApk(localPath);
    if (checked.error.isError())
        return failed<void>(checked.error);
    return m_files->installPackage(localPath, checked.name);
}

void AppsService::onUnsolicited(const QString &line)
{
    if (!line.startsWith(QLatin1StringView("*EJAVA:")))
        return;
    const Result<codec::AppEvent> event = codec::parseAppEvent(line);
    if (!event.hasValue()) {
        qCInfo(lcProtocol) << "App report not readable:" << event.error().message();
        return;
    }
    if (event.value().kind == codec::AppEvent::Kind::Removed)
        Q_EMIT appRemoved(event.value().app.package);
    else
        Q_EMIT appAdded(event.value().app);
}

} // namespace qpm::core::service
