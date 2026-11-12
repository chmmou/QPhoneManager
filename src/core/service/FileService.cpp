#include "core/service/FileService.h"

#include "core/Logging.h"
#include "core/protocol/ObexClient.h"

#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QPromise>
#include <QSaveFile>
#include <QStorageInfo>
#include <QTcpSocket>
#include <QTimer>
#include <QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <utility>

namespace qpm::core::service {

namespace {

using codec::FolderEntry;
using codec::FolderListings;
using protocol::ObexClient;
using protocol::ObexResponse;

template <typename T> using Done = std::function<void(const Result<T> &)>;

constexpr std::chrono::seconds listingTimeout{30};
constexpr std::chrono::seconds inlineTimeout{90};
constexpr std::chrono::seconds channelIdleTimeout{30};
constexpr qint64 channelChunkBytes = 256 * 1024;
constexpr qint64 channelBufferBytes = 1024 * 1024;
// One PUT packet holds the body and its headers, and the phone takes no
// upload spread over several packets.
constexpr qint64 maxInlineUploadBytes = 60000;
constexpr int statusTries = 3;
constexpr int uploadTries = 3;

template <typename T> std::pair<QFuture<Result<T>>, Done<T>> promised()
{
    auto promise = std::make_shared<QPromise<Result<T>>>();
    promise->start();
    QFuture<Result<T>> future = promise->future();
    return {future, [promise](const Result<T> &result) {
                if (promise->future().isFinished())
                    return;
                promise->addResult(result);
                promise->finish();
            }};
}

template <typename T> QFuture<Result<T>> failed(const Error &error)
{
    return QtFuture::makeReadyValueFuture(Result<T>(error));
}

Error notConnected()
{
    return Error(ErrorCode::ConnectionClosed, QStringLiteral("The phone is not connected"));
}

Error sessionGone()
{
    return Error(ErrorCode::ConnectionClosed, QStringLiteral("The OBEX session ended"));
}

Error cancelled()
{
    return Error(ErrorCode::Cancelled, QStringLiteral("The transfer was cancelled"));
}

Error unusableName()
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("The name cannot be used on the phone"));
}

bool isUsablePath(const QString &path)
{
    if (path.isEmpty())
        return true;
    if (!path.startsWith(u'\\'))
        return false;
    const QStringList parts = path.sliced(1).split(u'\\');
    return std::all_of(parts.cbegin(), parts.cend(), &codec::isUsableFileName);
}

/**
 * Goes to the root, which the phone needs before it lists anything, and from
 * there to `folder`, without creating it.
 */
void enterFolder(ObexClient &obex, const QString &folder, QObject *context, Done<void> done)
{
    QPointer<ObexClient> client = &obex;
    obex.setPath(QString(), ObexClient::SetPathFlag::DontCreate)
        .then(context, [client, folder, done](const Result<void> &root) {
            if (!root.hasValue() || folder.isEmpty()) {
                done(root);
                return;
            }
            if (!client) {
                done(sessionGone());
                return;
            }
            client->setPath(folder, ObexClient::SetPathFlag::DontCreate)
                .then([done](const Result<void> &entered) { done(entered); });
        });
}

} // namespace

struct FileService::Transfer
{
    bool upload = false;
    RemoteFile remote;
    QString localPath;
    QString remoteFolder;
    /// The name asked for; after an upload the name the phone chose.
    QString remoteName;
    QDateTime modified;
    quint64 total = 0;
    quint64 done = 0;
    quint64 sent = 0;
    bool cancelled = false;
    bool failing = false;
    bool settled = false;
    bool dataComplete = false;
    int uploadTriesLeft = uploadTries;
    int connectsLeft = sideChannelConnectTries;
    QPointer<ObexClient> obex;
    QPointer<QTcpSocket> socket;
    QPointer<QTimer> idle;
    std::unique_ptr<QSaveFile> target;
    std::unique_ptr<QFile> source;
    QMetaObject::Connection progressConnection;
    Done<void> finish;
};

FileService::FileService(protocol::PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    Q_ASSERT(connection);
    // A transfer on the side channel has no OBEX request open that would
    // fail with the connection.
    connect(connection, &protocol::PhoneConnection::stateChanged, this,
            [this](protocol::PhoneConnection::State state) {
                if (m_transfer && state != protocol::PhoneConnection::State::Ready &&
                    state != protocol::PhoneConnection::State::Busy) {
                    finishTransfer(m_transfer, notConnected());
                }
            });
}

void FileService::setEndpointResolver(EndpointResolver resolver)
{
    m_resolver = std::move(resolver);
}

std::optional<protocol::Endpoint> FileService::endpoint(quint16 phonePort) const
{
    if (m_resolver)
        return m_resolver(phonePort);
    return m_connection->sideChannelEndpoint(phonePort);
}

QString FileService::childPath(const QString &folder, const QString &name)
{
    return folder + u'\\' + name;
}

QString FileService::parentPath(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(u'\\');
    return slash <= 0 ? QString() : path.left(slash);
}

QString FileService::leafName(const QString &path)
{
    return path.mid(path.lastIndexOf(u'\\') + 1);
}

QFuture<Result<QList<FolderEntry>>> FileService::list(const QString &folder)
{
    return readListing(folder, ObexClient::ListingDepth::Folder)
        .then(this, [](const Result<FolderListings> &read) -> Result<QList<FolderEntry>> {
            if (!read.hasValue())
                return read.error();
            return read.value().folders.constFirst().entries;
        });
}

QFuture<Result<FolderListings>> FileService::listTree(const QString &folder)
{
    return readListing(folder, ObexClient::ListingDepth::Tree);
}

QFuture<Result<FolderListings>> FileService::readListing(const QString &folder,
                                                         ObexClient::ListingDepth depth)
{
    if (!isUsablePath(folder))
        return failed<FolderListings>(unusableName());
    if (!m_connection->session())
        return failed<FolderListings>(notConnected());
    return m_connection->withObex<FolderListings>(
        ObexClient::Target::FileBrowser, [this, folder, depth](ObexClient &obex) {
            auto [future, done] = promised<FolderListings>();
            QPointer<ObexClient> client = &obex;
            enterFolder(obex, folder, this, [this, client, depth, done](const Result<void> &in) {
                if (!in.hasValue()) {
                    done(in.error());
                    return;
                }
                if (!client) {
                    done(sessionGone());
                    return;
                }
                client->listFolder(depth, listingTimeout)
                    .then(
                        this,
                        [](const Result<ObexResponse> &answer) -> QFuture<Result<FolderListings>> {
                            if (!answer.hasValue())
                                return QtFuture::makeReadyValueFuture(
                                    Result<FolderListings>(answer.error()));
                            return QtConcurrent::run(&codec::parseFolderListing,
                                                     answer.value().body);
                        })
                    .unwrap()
                    .then(this, [done](const Result<FolderListings> &read) {
                        if (read.hasValue() && read.value().skipped > 0) {
                            qCInfo(lcProtocol)
                                << "Folder listing entries left out:" << read.value().skipped;
                        }
                        done(read);
                    });
            });
            return future;
        });
}

QFuture<Result<void>> FileService::makeFolder(const QString &parent, const QString &name)
{
    if (!isUsablePath(parent) || !codec::isUsableFileName(name))
        return failed<void>(unusableName());
    if (!m_connection->session())
        return failed<void>(notConnected());
    return m_connection->withObex<void>(
        ObexClient::Target::FileBrowser, [this, parent, name](ObexClient &obex) {
            auto [future, done] = promised<void>();
            QPointer<ObexClient> client = &obex;
            enterFolder(obex, parent, this, [client, name, done](const Result<void> &in) {
                if (!in.hasValue() || !client) {
                    done(in.hasValue() ? Result<void>(sessionGone()) : in);
                    return;
                }
                // Without "do not create" the phone creates the folder.
                client->setPath(name, ObexClient::SetPathFlag::None)
                    .then([done](const Result<void> &created) { done(created); });
            });
            return future;
        });
}

QFuture<Result<void>> FileService::rename(const QString &path, const QString &newName)
{
    if (path.isEmpty() || !isUsablePath(path) || parentPath(path).isEmpty() ||
        !codec::isUsableFileName(newName)) {
        return failed<void>(unusableName());
    }
    return moveTo(path, parentPath(path), newName);
}

QFuture<Result<void>> FileService::move(const QString &path, const QString &targetFolder)
{
    if (path.isEmpty() || !isUsablePath(path) || parentPath(path).isEmpty() ||
        targetFolder.isEmpty() || !isUsablePath(targetFolder)) {
        return failed<void>(unusableName());
    }
    if (targetFolder == path || targetFolder.startsWith(path + u'\\')) {
        return failed<void>(
            Error(ErrorCode::InvalidInput, QStringLiteral("A folder cannot go into itself")));
    }
    return moveTo(path, targetFolder, leafName(path));
}

QFuture<Result<void>> FileService::moveTo(const QString &path, const QString &targetFolder,
                                          const QString &name)
{
    if (!m_connection->session())
        return failed<void>(notConnected());
    // The phone resolves both names of a move from the storage roots.
    const QString target = childPath(targetFolder, name);
    return m_connection->withObex<void>(ObexClient::Target::FileBrowser, [this, path, target](
                                                                             ObexClient &obex) {
        auto [future, done] = promised<void>();
        QPointer<ObexClient> client = &obex;
        enterFolder(obex, QString(), this, [client, path, target, done](const Result<void> &in) {
            if (!in.hasValue() || !client) {
                done(in.hasValue() ? Result<void>(sessionGone()) : in);
                return;
            }
            client->move(path, target).then([done](const Result<void> &moved) { done(moved); });
        });
        return future;
    });
}

QFuture<Result<void>> FileService::remove(const QString &path)
{
    if (path.isEmpty() || !isUsablePath(path) || parentPath(path).isEmpty())
        return failed<void>(unusableName());
    if (!m_connection->session())
        return failed<void>(notConnected());
    return m_connection->withObex<void>(
        ObexClient::Target::FileBrowser, [this, path](ObexClient &obex) {
            auto [future, done] = promised<void>();
            QPointer<ObexClient> client = &obex;
            // The phone takes the name of a delete relative to the current folder.
            enterFolder(obex, parentPath(path), this, [client, path, done](const Result<void> &in) {
                if (!in.hasValue() || !client) {
                    done(in.hasValue() ? Result<void>(sessionGone()) : in);
                    return;
                }
                client->remove(leafName(path)).then([done](const Result<ObexResponse> &removed) {
                    done(removed.hasValue() ? Result<void>() : Result<void>(removed.error()));
                });
            });
            return future;
        });
}

QFuture<Result<void>> FileService::download(const RemoteFile &file, const QString &localPath)
{
    if (file.path.isEmpty() || !isUsablePath(file.path) || parentPath(file.path).isEmpty())
        return failed<void>(unusableName());
    if (!m_connection->session())
        return failed<void>(notConnected());
    if (m_transfer)
        return failed<void>(
            Error(ErrorCode::Rejected, QStringLiteral("Another transfer is running")));
    auto transfer = std::make_shared<Transfer>();
    transfer->remote = file;
    transfer->localPath = localPath;
    transfer->total = file.size;
    m_transfer = transfer;
    return m_connection->withObex<void>(
        ObexClient::Target::FileBrowser, [this, transfer](ObexClient &obex) {
            auto [future, done] = promised<void>();
            transfer->finish = done;
            transfer->obex = &obex;
            enterFolder(obex, parentPath(transfer->remote.path), this,
                        [this, transfer](const Result<void> &in) {
                            if (!in.hasValue()) {
                                finishTransfer(transfer, in);
                            } else if (transfer->cancelled) {
                                finishTransfer(transfer, cancelled());
                            } else if (!transfer->obex) {
                                finishTransfer(transfer, sessionGone());
                            } else if (endpoint(fileChannelPort)) {
                                runSocketDownload(transfer, *transfer->obex);
                            } else {
                                runInlineDownload(transfer, *transfer->obex);
                            }
                        });
            return future;
        });
}

QFuture<Result<QString>> FileService::upload(const QString &localPath, const QString &remoteFolder,
                                             const QString &remoteName)
{
    if (remoteFolder.isEmpty() || !isUsablePath(remoteFolder) ||
        !codec::isUsableFileName(remoteName)) {
        return failed<QString>(unusableName());
    }
    return startUpload(localPath, remoteFolder, remoteName);
}

QFuture<Result<void>> FileService::installPackage(const QString &localPath,
                                                  const QString &remoteName)
{
    if (!codec::isUsableFileName(remoteName) ||
        !remoteName.endsWith(QLatin1StringView(".apk"), Qt::CaseInsensitive)) {
        return failed<void>(unusableName());
    }
    return startUpload(localPath, QString(), remoteName)
        .then(this, [](const Result<QString> &sent) -> Result<void> {
            if (!sent.hasValue())
                return sent.error();
            return {};
        });
}

QFuture<Result<QString>> FileService::startUpload(const QString &localPath,
                                                  const QString &remoteFolder,
                                                  const QString &remoteName)
{
    if (!m_connection->session())
        return failed<QString>(notConnected());
    if (m_transfer)
        return failed<QString>(
            Error(ErrorCode::Rejected, QStringLiteral("Another transfer is running")));
    auto source = std::make_unique<QFile>(localPath);
    if (!source->open(QIODevice::ReadOnly)) {
        return failed<QString>(
            Error(ErrorCode::IoFailure, QStringLiteral("The local file cannot be read")));
    }
    auto transfer = std::make_shared<Transfer>();
    transfer->upload = true;
    transfer->localPath = localPath;
    transfer->remoteFolder = remoteFolder;
    transfer->remoteName = remoteName;
    transfer->total = quint64(source->size());
    transfer->modified = QFileInfo(localPath).lastModified().toUTC();
    transfer->source = std::move(source);
    m_transfer = transfer;
    return m_connection
        ->withObex<void>(ObexClient::Target::FileBrowser,
                         [this, transfer](ObexClient &obex) {
                             auto [future, done] = promised<void>();
                             transfer->finish = done;
                             transfer->obex = &obex;
                             enterFolder(obex, transfer->remoteFolder, this,
                                         [this, transfer](const Result<void> &in) {
                                             if (!in.hasValue()) {
                                                 finishTransfer(transfer, in);
                                             } else if (transfer->cancelled) {
                                                 finishTransfer(transfer, cancelled());
                                             } else if (!transfer->obex) {
                                                 finishTransfer(transfer, sessionGone());
                                             } else if (endpoint(fileChannelPort)) {
                                                 runSocketUpload(transfer, *transfer->obex);
                                             } else {
                                                 runInlineUpload(transfer, *transfer->obex);
                                             }
                                         });
                             return future;
                         })
        .then(this, [transfer](const Result<void> &result) -> Result<QString> {
            if (!result.hasValue())
                return result.error();
            return transfer->remoteName;
        });
}

void FileService::cancelTransfer()
{
    const std::shared_ptr<Transfer> transfer = m_transfer;
    if (!transfer || transfer->settled || transfer->cancelled)
        return;
    transfer->cancelled = true;
    if (!transfer->obex) {
        finishTransfer(transfer, cancelled());
        return;
    }
    if (transfer->socket) {
        transfer->socket->disconnect(this);
        transfer->socket->abort();
        // The phone drops a half written upload on ABORT.
        transfer->obex->abort().then(this, [this, transfer](const Result<void> &) {
            finishTransfer(transfer, cancelled());
        });
        return;
    }
    // Aborts the request that runs, which then reports the cancellation.
    static_cast<void>(transfer->obex->abort());
}

void FileService::finishTransfer(const std::shared_ptr<Transfer> &ended, const Result<void> &result)
{
    // A copy, since `ended` may be m_transfer itself, which is reset below.
    const std::shared_ptr<Transfer> transfer = ended;
    if (transfer->settled)
        return;
    transfer->settled = true;
    disconnect(transfer->progressConnection);
    if (transfer->idle)
        transfer->idle->deleteLater();
    if (transfer->socket) {
        transfer->socket->disconnect(this);
        transfer->socket->abort();
        transfer->socket->deleteLater();
    }
    // An uncommitted file is discarded with its object.
    transfer->target.reset();
    transfer->source.reset();
    if (m_transfer == transfer)
        m_transfer.reset();
    if (!result.hasValue())
        qCInfo(lcProtocol) << "File transfer ended:" << result.error().message();
    if (transfer->finish)
        transfer->finish(result);
}

namespace {

// Fails a transfer whose side channel the phone already prepared: an ABORT
// makes the phone drop it, and the failure is reported after its answer.
void abortThen(QObject *context, const QPointer<ObexClient> &obex, std::function<void()> then)
{
    if (!obex) {
        then();
        return;
    }
    obex->abort().then(context, [then](const Result<void> &) { then(); });
}

} // namespace

void FileService::runSocketDownload(const std::shared_ptr<Transfer> &transfer, ObexClient &obex)
{
    obex.socketGet(leafName(transfer->remote.path))
        .then(this, [this, transfer](const Result<ObexResponse> &answer) {
            if (transfer->settled)
                return;
            if (!answer.hasValue()) {
                finishTransfer(transfer, transfer->cancelled ? cancelled() : answer.error());
                return;
            }
            const auto fail = [this, transfer](const Error &error) {
                if (std::exchange(transfer->failing, true) || transfer->settled)
                    return;
                abortThen(this, transfer->obex,
                          [this, transfer, error]() { finishTransfer(transfer, error); });
            };
            const std::optional<quint16> port =
                ObexClient::sideChannelPort(answer.value().appParams);
            const std::optional<quint64> length = answer.value().length;
            if (!port || !length) {
                fail(Error(ErrorCode::ProtocolError,
                           QStringLiteral("The phone named no channel for the file")));
                return;
            }
            if (*length > maxChannelFileBytes) {
                fail(Error(ErrorCode::LimitExceeded,
                           QStringLiteral("File of %1 bytes is too large").arg(*length)));
                return;
            }
            const QStorageInfo storage(QFileInfo(transfer->localPath).absolutePath());
            if (storage.isValid() && storage.bytesAvailable() >= 0 &&
                quint64(storage.bytesAvailable()) < *length) {
                fail(Error(ErrorCode::IoFailure,
                           QStringLiteral("Not enough space on this computer")));
                return;
            }
            const std::optional<protocol::Endpoint> side = endpoint(*port);
            if (!side) {
                fail(Error(ErrorCode::NotSupported,
                           QStringLiteral("The fast channel cannot be reached")));
                return;
            }
            transfer->target = std::make_unique<QSaveFile>(transfer->localPath);
            if (!transfer->target->open(QIODevice::WriteOnly)) {
                fail(Error(ErrorCode::IoFailure, QStringLiteral("The target cannot be written")));
                return;
            }
            transfer->total = *length;
            Q_EMIT transferProgress(0, transfer->total);

            // Plain TCP, although network connections are meant to use TLS: the phone app
            // offers this channel without it, and the program has to work with that app.
            auto *socket = new QTcpSocket(this);
            auto *idle = new QTimer(this);
            transfer->socket = socket;
            transfer->idle = idle;
            idle->setSingleShot(true);
            connect(idle, &QTimer::timeout, this, [fail]() {
                fail(Error(ErrorCode::Timeout, QStringLiteral("The file transfer stalled")));
            });
            const auto complete = [this, transfer]() {
                transfer->dataComplete = true;
                if (transfer->idle)
                    transfer->idle->stop();
                // The phone waits for this side to close before it ends the transfer.
                transfer->socket->disconnect(this);
                transfer->socket->disconnectFromHost();
                settleSideChannel(transfer, statusTries);
            };
            connect(socket, &QTcpSocket::connected, this, [transfer, complete]() {
                if (transfer->total == 0)
                    complete();
            });
            connect(socket, &QTcpSocket::readyRead, this,
                    [this, transfer, socket, complete, fail]() {
                        const qint64 wanted = qint64(std::min<quint64>(
                            quint64(socket->bytesAvailable()), transfer->total - transfer->done));
                        const QByteArray chunk = socket->read(wanted);
                        if (transfer->target->write(chunk) != chunk.size()) {
                            fail(Error(ErrorCode::IoFailure,
                                       QStringLiteral("The target cannot be written")));
                            return;
                        }
                        transfer->done += quint64(chunk.size());
                        transfer->idle->start(channelIdleTimeout);
                        Q_EMIT transferProgress(transfer->done, transfer->total);
                        if (transfer->done == transfer->total)
                            complete();
                    });
            // A channel that ends before its first byte did not reach the phone's
            // listener yet; the phone keeps it open and is asked again.
            const protocol::Endpoint target = *side;
            const auto missed = [transfer, socket, target, fail](const Error &error) {
                if (transfer->done > 0 || --transfer->connectsLeft <= 0) {
                    fail(error);
                    return;
                }
                QTimer::singleShot(sideChannelRetryPause, socket, [socket, target]() {
                    socket->connectToHost(target.host, target.port);
                });
            };
            connect(socket, &QTcpSocket::disconnected, this, [missed]() {
                missed(Error(ErrorCode::ConnectionClosed,
                             QStringLiteral("The file transfer broke off")));
            });
            connect(socket, &QTcpSocket::errorOccurred, this, [socket, missed]() {
                // A close by the phone also ends in disconnected().
                if (socket->error() != QAbstractSocket::RemoteHostClosedError &&
                    socket->state() != QAbstractSocket::ConnectedState) {
                    missed(Error(ErrorCode::ConnectionClosed,
                                 QStringLiteral("The fast channel cannot be reached")));
                }
            });
            idle->start(channelIdleTimeout);
            socket->connectToHost(target.host, target.port);
        });
}

void FileService::runInlineDownload(const std::shared_ptr<Transfer> &transfer, ObexClient &obex)
{
    transfer->target = std::make_unique<QSaveFile>(transfer->localPath);
    if (!transfer->target->open(QIODevice::WriteOnly)) {
        finishTransfer(transfer,
                       Error(ErrorCode::IoFailure, QStringLiteral("The target cannot be written")));
        return;
    }
    transfer->progressConnection = connect(
        &obex, &ObexClient::progress, this, [this, transfer](quint64 done, std::optional<quint64>) {
            transfer->done = done;
            Q_EMIT transferProgress(done, std::max(transfer->total, done));
        });
    obex.get(leafName(transfer->remote.path), {}, inlineTimeout)
        .then(this, [this, transfer](const Result<ObexResponse> &answer) {
            if (transfer->settled)
                return;
            if (!answer.hasValue()) {
                finishTransfer(transfer, transfer->cancelled ? cancelled() : answer.error());
                return;
            }
            const QByteArray &body = answer.value().body;
            if (transfer->target->write(body) != body.size()) {
                finishTransfer(transfer, Error(ErrorCode::IoFailure,
                                               QStringLiteral("The target cannot be written")));
                return;
            }
            transfer->total = quint64(body.size());
            transfer->done = transfer->total;
            transfer->dataComplete = true;
            keepTransfer(transfer);
        });
}

void FileService::runSocketUpload(const std::shared_ptr<Transfer> &transfer, ObexClient &obex)
{
    obex.socketPut(transfer->remoteName, transfer->total, transfer->modified)
        .then(this, [this, transfer](const Result<ObexResponse> &answer) {
            if (transfer->settled)
                return;
            if (!answer.hasValue()) {
                finishTransfer(transfer, transfer->cancelled ? cancelled() : answer.error());
                return;
            }
            const auto fail = [this, transfer](const Error &error) {
                if (std::exchange(transfer->failing, true) || transfer->settled)
                    return;
                abortThen(this, transfer->obex,
                          [this, transfer, error]() { finishTransfer(transfer, error); });
            };
            if (codec::isUsableFileName(answer.value().name))
                transfer->remoteName = answer.value().name;
            const std::optional<quint16> port =
                ObexClient::sideChannelPort(answer.value().appParams);
            const std::optional<protocol::Endpoint> side = port ? endpoint(*port) : std::nullopt;
            if (!side) {
                fail(Error(ErrorCode::NotSupported,
                           QStringLiteral("The fast channel cannot be reached")));
                return;
            }
            Q_EMIT transferProgress(0, transfer->total);

            // Plain TCP, although network connections are meant to use TLS: the phone app
            // offers this channel without it, and the program has to work with that app.
            auto *socket = new QTcpSocket(this);
            auto *idle = new QTimer(this);
            transfer->socket = socket;
            transfer->idle = idle;
            idle->setSingleShot(true);
            connect(idle, &QTimer::timeout, this, [fail]() {
                fail(Error(ErrorCode::Timeout, QStringLiteral("The file transfer stalled")));
            });
            const auto pump = [this, transfer, socket, fail]() {
                while (transfer->sent < transfer->total &&
                       socket->bytesToWrite() < channelBufferBytes) {
                    const qint64 size = qint64(
                        std::min<quint64>(channelChunkBytes, transfer->total - transfer->sent));
                    const QByteArray chunk = transfer->source->read(size);
                    if (chunk.size() != size) {
                        fail(Error(ErrorCode::IoFailure,
                                   QStringLiteral("The local file cannot be read")));
                        return;
                    }
                    socket->write(chunk);
                    transfer->sent += quint64(chunk.size());
                }
                transfer->done = transfer->sent - quint64(socket->bytesToWrite());
                Q_EMIT transferProgress(transfer->done, transfer->total);
                // Closing this side right behind the data loses the data over adb
                // forwarding, so only the phone closes, once it has read the length
                // announced. An empty upload has no length to reach and ends on
                // this side's close.
                if (transfer->total == 0 && socket->state() == QAbstractSocket::ConnectedState)
                    socket->disconnectFromHost();
            };
            connect(socket, &QTcpSocket::connected, this, pump);
            connect(socket, &QTcpSocket::bytesWritten, this, [transfer, pump]() {
                transfer->idle->start(channelIdleTimeout);
                pump();
            });
            // A close before everything was written means the phone did not take
            // the data. Over adb forwarding that is also how a channel the phone
            // does not listen on yet looks.
            const auto closed = [this, transfer, socket]() {
                if (transfer->sent == transfer->total && socket->bytesToWrite() == 0) {
                    if (!transfer->dataComplete) {
                        transfer->dataComplete = true;
                        transfer->idle->stop();
                        socket->disconnect(this);
                        settleSideChannel(transfer, statusTries);
                    }
                    return;
                }
                repeatUpload(transfer, Error(ErrorCode::ConnectionClosed,
                                             QStringLiteral("The file transfer broke off")));
            };
            connect(socket, &QTcpSocket::disconnected, this, closed);
            connect(socket, &QTcpSocket::errorOccurred, this, [this, transfer, socket, closed]() {
                if (socket->error() == QAbstractSocket::RemoteHostClosedError) {
                    closed();
                } else if (socket->state() != QAbstractSocket::ConnectedState) {
                    repeatUpload(transfer,
                                 Error(ErrorCode::ConnectionClosed,
                                       QStringLiteral("The fast channel cannot be reached")));
                }
            });
            idle->start(channelIdleTimeout);
            socket->connectToHost(side->host, side->port);
        });
}

void FileService::runInlineUpload(const std::shared_ptr<Transfer> &transfer, ObexClient &obex)
{
    // An empty PUT body deletes the file on the phone.
    if (transfer->total == 0 || qint64(transfer->total) > maxInlineUploadBytes) {
        finishTransfer(transfer, Error(ErrorCode::NotSupported,
                                       QStringLiteral("This file needs the fast channel")));
        return;
    }
    const QByteArray data = transfer->source->readAll();
    if (quint64(data.size()) != transfer->total) {
        finishTransfer(
            transfer, Error(ErrorCode::IoFailure, QStringLiteral("The local file cannot be read")));
        return;
    }
    Q_EMIT transferProgress(0, transfer->total);
    ObexClient::PutOptions options;
    options.modified = transfer->modified;
    obex.put(transfer->remoteName, data, options)
        .then(this, [this, transfer](const Result<ObexResponse> &answer) {
            if (!answer.hasValue()) {
                finishTransfer(transfer, transfer->cancelled ? cancelled() : answer.error());
                return;
            }
            transfer->done = transfer->total;
            Q_EMIT transferProgress(transfer->done, transfer->total);
            finishTransfer(transfer, Result<void>());
        });
}

void FileService::keepTransfer(const std::shared_ptr<Transfer> &transfer)
{
    if (transfer->upload) {
        finishTransfer(transfer, Result<void>());
        return;
    }
    if (!transfer->target->commit()) {
        finishTransfer(transfer,
                       Error(ErrorCode::IoFailure, QStringLiteral("The target cannot be written")));
        return;
    }
    if (transfer->remote.modified.isValid()) {
        QFile written(transfer->localPath);
        if (!written.open(QIODevice::ReadWrite) ||
            !written.setFileTime(transfer->remote.modified, QFileDevice::FileModificationTime)) {
            qCInfo(lcProtocol) << "Modification time of a download not set";
        }
    }
    Q_EMIT transferProgress(transfer->total, transfer->total);
    finishTransfer(transfer, Result<void>());
}

void FileService::settleSideChannel(const std::shared_ptr<Transfer> &transfer, int triesLeft)
{
    if (!transfer->obex) {
        finishTransfer(transfer, sessionGone());
        return;
    }
    // The phone waits about ten seconds for its side of the transfer and
    // answers "timeout" when that is not enough. An upload is only settled
    // after the phone closed the channel, so there the answer means that no
    // connection reached the phone; it still keeps the empty file then.
    transfer->obex->socketStatus().then(
        this, [this, transfer, triesLeft](const Result<void> &status) {
            if (transfer->settled)
                return;
            if (status.hasValue()) {
                keepTransfer(transfer);
                return;
            }
            const bool phoneTimeout = status.error().code() == ErrorCode::Timeout &&
                                      transfer->obex && transfer->obex->isConnected();
            if (phoneTimeout && transfer->upload) {
                repeatUpload(transfer, status.error());
                return;
            }
            if (phoneTimeout && triesLeft > 1) {
                settleSideChannel(transfer, triesLeft - 1);
                return;
            }
            finishTransfer(transfer, transfer->cancelled ? cancelled() : status.error());
        });
}

void FileService::repeatUpload(const std::shared_ptr<Transfer> &transfer, const Error &error)
{
    if (std::exchange(transfer->failing, true) || transfer->settled)
        return;
    if (transfer->idle)
        transfer->idle->deleteLater();
    if (transfer->socket) {
        transfer->socket->disconnect(this);
        transfer->socket->deleteLater();
    }
    transfer->idle = nullptr;
    transfer->socket = nullptr;
    const bool again = --transfer->uploadTriesLeft > 0;
    const QPointer<ObexClient> client = transfer->obex;
    // ABORT makes the phone drop a file it has not closed yet; one it closed
    // after a "timeout" answer stays and is deleted. Its name is the one the
    // phone picked for this upload.
    abortThen(this, client, [this, transfer, client, error, again]() {
        const auto next = [this, transfer, error, again]() {
            if (transfer->settled)
                return;
            if (transfer->cancelled) {
                finishTransfer(transfer, cancelled());
                return;
            }
            if (!again || !transfer->obex || !transfer->source->seek(0)) {
                finishTransfer(transfer, error);
                return;
            }
            qCInfo(lcProtocol) << "File upload started again:" << error.message();
            transfer->sent = 0;
            transfer->done = 0;
            transfer->dataComplete = false;
            transfer->failing = false;
            runSocketUpload(transfer, *transfer->obex);
        };
        if (!client || !client->isConnected()) {
            next();
            return;
        }
        client->remove(transfer->remoteName).then(this, [next](const Result<ObexResponse> &) {
            next();
        });
    });
}

} // namespace qpm::core::service
