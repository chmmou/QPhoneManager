#include "core/service/ThumbnailChannel.h"

#include "core/Logging.h"
#include "core/protocol/ObexClient.h"

#include <QPointer>
#include <QPromise>
#include <QTcpSocket>
#include <QTimer>
#include <QXmlStreamWriter>

#include <algorithm>
#include <chrono>
#include <memory>

namespace qpm::core::service {

namespace {

using protocol::ObexClient;
using protocol::ObexResponse;

constexpr QByteArrayView fileBegin("FileBegin;");
constexpr QByteArrayView fileEnd("\r\nFileEnd");
constexpr QByteArrayView imageEnd("\r\nFileEndFileBegin;");
constexpr QByteArrayView orientationField(";Orientation=");
constexpr QByteArrayView endOfStream("EndOfStream");
constexpr qsizetype maxHeaderBytes = 4096;
constexpr int maxOrientation = 8;
constexpr std::chrono::seconds streamIdleTimeout{20};

Error malformed(const QString &what)
{
    return Error(ErrorCode::ProtocolError, QStringLiteral("Thumbnail stream: %1").arg(what));
}

QFuture<Result<QList<Thumbnail>>> failedFetch(const Error &error)
{
    return QtFuture::makeReadyValueFuture(Result<QList<Thumbnail>>(error));
}

} // namespace

ThumbnailStreamReader::ThumbnailStreamReader(const QStringList &expectedNames)
    : m_expected(expectedNames)
{
}

Result<void> ThumbnailStreamReader::feed(const QByteArray &bytes)
{
    if (m_error.isError())
        return m_error;
    if (m_finished)
        return {};
    m_buffer += bytes;
    bool progressed = true;
    while (progressed && !m_finished) {
        const Result<void> stepped = step(progressed);
        if (!stepped.hasValue()) {
            m_error = stepped.error();
            m_buffer.clear();
            return m_error;
        }
    }
    return {};
}

Result<void> ThumbnailStreamReader::step(bool &progressed)
{
    progressed = false;
    if (!m_inImage) {
        if (m_buffer.size() < fileBegin.size()) {
            if (!fileBegin.startsWith(m_buffer))
                return malformed(QStringLiteral("no file header"));
            return {};
        }
        if (!m_buffer.startsWith(fileBegin))
            return malformed(QStringLiteral("no file header"));
        const qsizetype lineEnd = m_buffer.indexOf("\r\n");
        if (lineEnd < 0) {
            if (m_buffer.size() > maxHeaderBytes)
                return Error(ErrorCode::LimitExceeded,
                             QStringLiteral("Thumbnail header line too long"));
            return {};
        }
        const QByteArray line = m_buffer.sliced(fileBegin.size(), lineEnd - fileBegin.size());
        m_buffer.remove(0, lineEnd + 2);
        progressed = true;
        if (line == endOfStream) {
            m_finished = true;
            return {};
        }
        const qsizetype field = line.lastIndexOf(orientationField);
        if (field < 0)
            return malformed(QStringLiteral("header without orientation"));
        bool ok = false;
        const int orientation = line.sliced(field + orientationField.size()).toInt(&ok);
        const QString path = QString::fromUtf8(line.left(field));
        const QString name = path.mid(path.lastIndexOf(u'\\') + 1);
        const qsizetype expected = m_expected.indexOf(name);
        if (expected < 0)
            return malformed(QStringLiteral("a file that was not asked for"));
        m_expected.removeAt(expected);
        m_current = Thumbnail();
        m_current.name = name;
        m_current.orientation =
            ok && orientation >= 0 && orientation <= maxOrientation ? orientation : 0;
        m_inImage = true;
        return {};
    }
    const qsizetype end = m_buffer.indexOf(imageEnd);
    if (end < 0) {
        if (m_buffer.size() > maxImageBytes + imageEnd.size())
            return Error(ErrorCode::LimitExceeded, QStringLiteral("Thumbnail too large"));
        return {};
    }
    if (end > maxImageBytes)
        return Error(ErrorCode::LimitExceeded, QStringLiteral("Thumbnail too large"));
    m_current.image = m_buffer.left(end);
    m_buffer.remove(0, end + fileEnd.size());
    m_thumbnails.append(m_current);
    m_inImage = false;
    progressed = true;
    return {};
}

ThumbnailChannel::ThumbnailChannel(protocol::PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_connection(connection)
{
    Q_ASSERT(connection);
}

void ThumbnailChannel::setEndpointResolver(FileService::EndpointResolver resolver)
{
    m_resolver = std::move(resolver);
}

QByteArray ThumbnailChannel::requestBody(const QString &folder, const QStringList &names)
{
    QByteArray body;
    QXmlStreamWriter writer(&body);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("thumbnails"));
    writer.writeEmptyElement(QStringLiteral("folder"));
    writer.writeAttribute(QStringLiteral("path"), folder);
    for (const QString &name : names) {
        writer.writeEmptyElement(QStringLiteral("file"));
        writer.writeAttribute(QStringLiteral("name"), name);
    }
    writer.writeEndElement();
    writer.writeEndDocument();
    return body;
}

QFuture<Result<QList<Thumbnail>>> ThumbnailChannel::fetch(const QString &folder,
                                                          const QStringList &names)
{
    if (names.isEmpty() || names.size() > maxNamesPerRequest ||
        !std::all_of(names.cbegin(), names.cend(), &codec::isUsableFileName) ||
        !folder.startsWith(u'\\')) {
        return failedFetch(
            Error(ErrorCode::InvalidInput, QStringLiteral("Unusable thumbnail request")));
    }
    if (!m_connection->session())
        return failedFetch(
            Error(ErrorCode::ConnectionClosed, QStringLiteral("The phone is not connected")));
    const auto resolve = [this](quint16 port) {
        return m_resolver ? m_resolver(port) : m_connection->sideChannelEndpoint(port);
    };
    if (!resolve(thumbnailPort))
        return failedFetch(Error(ErrorCode::NotSupported,
                                 QStringLiteral("Thumbnails need a connection over the network")));

    return m_connection->withObex<QList<Thumbnail>>(
        ObexClient::Target::FileBrowser, [this, folder, names, resolve](ObexClient &obex) {
            auto promise = std::make_shared<QPromise<Result<QList<Thumbnail>>>>();
            promise->start();
            const auto done = [promise](const Result<QList<Thumbnail>> &result) {
                if (promise->future().isFinished())
                    return;
                promise->addResult(result);
                promise->finish();
            };
            obex.put(QStringLiteral("\\getthumbnail"), requestBody(folder, names))
                .then(this, [this, names, resolve, done](const Result<ObexResponse> &answer) {
                    if (!answer.hasValue()) {
                        // The phone answers "bad request" for a folder it does not serve.
                        done(answer.error().code() == ErrorCode::ProtocolError
                                 ? Error(ErrorCode::Rejected,
                                         QStringLiteral("The phone makes no thumbnails here"))
                                 : answer.error());
                        return;
                    }
                    const std::optional<quint16> port =
                        ObexClient::sideChannelPort(answer.value().appParams);
                    const std::optional<protocol::Endpoint> side =
                        port ? resolve(*port) : std::nullopt;
                    if (!side) {
                        done(Error(ErrorCode::ProtocolError,
                                   QStringLiteral("The phone named no thumbnail channel")));
                        return;
                    }
                    // Plain TCP, although network connections are meant to use TLS: the phone app
                    // offers this channel without it, and the program has to work with that app.
                    auto *socket = new QTcpSocket(this);
                    auto *idle = new QTimer(socket);
                    auto reader = std::make_shared<ThumbnailStreamReader>(names);
                    idle->setSingleShot(true);
                    const auto finish = [socket, done](const Result<QList<Thumbnail>> &result) {
                        socket->disconnect();
                        // The phone ends its side when this one closes.
                        socket->disconnectFromHost();
                        socket->deleteLater();
                        done(result);
                    };
                    connect(idle, &QTimer::timeout, socket, [finish]() {
                        finish(Error(ErrorCode::Timeout,
                                     QStringLiteral("The thumbnail stream stalled")));
                    });
                    auto received = std::make_shared<bool>(false);
                    connect(socket, &QTcpSocket::readyRead, socket,
                            [socket, idle, reader, received, finish]() {
                                *received = true;
                                idle->start(streamIdleTimeout);
                                const Result<void> fed = reader->feed(socket->readAll());
                                if (!fed.hasValue())
                                    finish(fed.error());
                                else if (reader->isFinished())
                                    finish(reader->thumbnails());
                            });
                    // A stream that ends before its first byte did not reach the
                    // phone's listener yet; the phone keeps it open and is asked again.
                    const protocol::Endpoint target = *side;
                    auto connectsLeft = std::make_shared<int>(FileService::sideChannelConnectTries);
                    const auto missed = [socket, target, received, connectsLeft,
                                         finish](const Error &error) {
                        if (*received || --*connectsLeft <= 0) {
                            finish(error);
                            return;
                        }
                        QTimer::singleShot(FileService::sideChannelRetryPause, socket,
                                           [socket, target]() {
                                               socket->connectToHost(target.host, target.port);
                                           });
                    };
                    connect(socket, &QTcpSocket::disconnected, socket, [missed]() {
                        missed(Error(ErrorCode::ConnectionClosed,
                                     QStringLiteral("The thumbnail stream broke off")));
                    });
                    connect(socket, &QTcpSocket::errorOccurred, socket, [socket, missed]() {
                        // A close by the phone also ends in disconnected().
                        if (socket->error() != QAbstractSocket::RemoteHostClosedError &&
                            socket->state() != QAbstractSocket::ConnectedState) {
                            missed(
                                Error(ErrorCode::ConnectionClosed,
                                      QStringLiteral("The thumbnail channel cannot be reached")));
                        }
                    });
                    idle->start(streamIdleTimeout);
                    socket->connectToHost(target.host, target.port);
                });
            return promise->future();
        });
}

} // namespace qpm::core::service
