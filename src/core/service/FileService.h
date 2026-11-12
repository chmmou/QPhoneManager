#pragma once

#include "core/Error.h"
#include "core/codec/FolderListing.h"
#include "core/protocol/PhoneConnection.h"

#include <QDateTime>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace qpm::core::service {

/// A file on the phone, as a listing gives it.
struct RemoteFile
{
    /// Full path such as "\sdcard\DCIM\a.jpg".
    QString path;
    quint64 size = 0;
    /// UTC; invalid when unknown.
    QDateTime modified;
};

/**
 * The file browser of the connected phone: folder listings, reading and
 * writing files, folders, renaming, moving and deleting.
 *
 * Paths name a folder or file with backslashes from a storage root, as the
 * phone does: "\sdcard\DCIM\a.jpg"; the empty path is the root, which lists
 * the storages "phone", "sdcard", "external" and "dual" that the phone has.
 * A path is never percent-encoded, and no path ends with a backslash.
 *
 * Every call runs in an OBEX session of its own and fails with
 * ErrorCode::ConnectionClosed when the phone is not connected; a name that
 * codec::isUsableFileName() refuses fails with ErrorCode::InvalidInput
 * before anything is sent. Error messages name no file.
 *
 * Files go over the phone's fast TCP channel where one can be reached, and
 * otherwise inside the OBEX session, where an upload is limited to what fits
 * into one packet. One transfer runs at a time; transferProgress() reports
 * it and cancelTransfer() ends it, which also makes the phone drop a half
 * written upload. A download writes to a temporary file next to the target
 * and replaces the target only when complete, then sets its modification
 * time.
 *
 * Lives in the main thread; no call blocks. Does not own the connection,
 * which must outlive it.
 */
class FileService : public QObject
{
    Q_OBJECT

public:
    /// Where a TCP side channel of the phone is reached; see
    /// PhoneConnection::sideChannelEndpoint().
    using EndpointResolver = std::function<std::optional<protocol::Endpoint>(quint16 phonePort)>;

    /// Port of the fast file channel as the phone names it.
    static constexpr quint16 fileChannelPort = 5216;
    /// Largest file read over the fast channel.
    static constexpr quint64 maxChannelFileBytes = 64ULL * 1024 * 1024 * 1024;
    /**
     * Connections tried, and the pause between them, when a side channel the
     * phone sends on closes before its first byte. The phone opens its
     * listener only after it answered, and adb forwarding takes a connection
     * before that and closes it again. The phone keeps listening for ten
     * seconds.
     */
    static constexpr int sideChannelConnectTries = 20;
    static constexpr std::chrono::milliseconds sideChannelRetryPause{250};

    explicit FileService(protocol::PhoneConnection *connection, QObject *parent = nullptr);

    /**
     * Replaces how the side channels are found, for a connection whose
     * transport cannot tell, such as a test through a guard over adb.
     */
    void setEndpointResolver(EndpointResolver resolver);
    [[nodiscard]] std::optional<protocol::Endpoint> endpoint(quint16 phonePort) const;

    /// "\sdcard" and "a.jpg" give "\sdcard\a.jpg"; the root and "sdcard" give "\sdcard".
    [[nodiscard]] static QString childPath(const QString &folder, const QString &name);
    /// The folder that holds `path`; the root for a storage.
    [[nodiscard]] static QString parentPath(const QString &path);
    /// The last part of `path`.
    [[nodiscard]] static QString leafName(const QString &path);

    /// The entries of `folder`.
    QFuture<Result<QList<codec::FolderEntry>>> list(const QString &folder);

    /// The entries of `folder` and of every folder below it, one block per folder.
    QFuture<Result<codec::FolderListings>> listTree(const QString &folder);

    /**
     * Creates the folder `name` in `parent`. Succeeds as well when it exists
     * already, since the phone does not tell the difference.
     */
    QFuture<Result<void>> makeFolder(const QString &parent, const QString &name);

    /// Gives `path` the name `newName` in the same folder; ErrorCode::Rejected when it is taken.
    QFuture<Result<void>> rename(const QString &path, const QString &newName);

    /// Moves `path` into `targetFolder`; ErrorCode::Rejected when the name is taken there.
    QFuture<Result<void>> move(const QString &path, const QString &targetFolder);

    /// Deletes a file, or a folder with everything in it.
    QFuture<Result<void>> remove(const QString &path);

    /**
     * Reads `file` into `localPath`, which may exist and is then replaced.
     * The folder of `localPath` must exist. Fails with ErrorCode::IoFailure
     * when this computer cannot write it or has too little space, and with
     * ErrorCode::Cancelled after cancelTransfer().
     */
    QFuture<Result<void>> download(const RemoteFile &file, const QString &localPath);

    /**
     * Writes the local file `localPath` into `remoteFolder` as `remoteName`,
     * with its modification time. The phone never overwrites: when the name
     * is taken it picks a free one like "a (1).txt", which the result gives.
     * When the phone did not receive the data over the fast channel, the
     * upload starts again from the beginning, three times in all, and
     * transferProgress() starts again at zero.
     */
    QFuture<Result<QString>> upload(const QString &localPath, const QString &remoteFolder,
                                    const QString &remoteName);

    /// Ends the running transfer; its future then fails with ErrorCode::Cancelled.
    void cancelTransfer();

Q_SIGNALS:
    /// Bytes of the running transfer done so far, of `bytesTotal`.
    void transferProgress(quint64 bytesDone, quint64 bytesTotal);

private:
    struct Transfer;

    QFuture<Result<codec::FolderListings>> readListing(const QString &folder,
                                                       protocol::ObexClient::ListingDepth depth);
    QFuture<Result<void>> moveTo(const QString &path, const QString &targetFolder,
                                 const QString &name);
    void keepTransfer(const std::shared_ptr<Transfer> &transfer);
    void finishTransfer(const std::shared_ptr<Transfer> &transfer, const Result<void> &result);

    void runSocketDownload(const std::shared_ptr<Transfer> &transfer, protocol::ObexClient &obex);
    void runInlineDownload(const std::shared_ptr<Transfer> &transfer, protocol::ObexClient &obex);
    void runSocketUpload(const std::shared_ptr<Transfer> &transfer, protocol::ObexClient &obex);
    void runInlineUpload(const std::shared_ptr<Transfer> &transfer, protocol::ObexClient &obex);
    void settleSideChannel(const std::shared_ptr<Transfer> &transfer, int triesLeft);
    /**
     * Removes what the phone kept of an upload it did not receive, then
     * starts it again, or fails it with `error` when no try is left.
     */
    void repeatUpload(const std::shared_ptr<Transfer> &transfer, const Error &error);

    protocol::PhoneConnection *m_connection = nullptr;
    EndpointResolver m_resolver;
    std::shared_ptr<Transfer> m_transfer;
};

} // namespace qpm::core::service
