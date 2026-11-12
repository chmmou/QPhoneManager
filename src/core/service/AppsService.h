#pragma once

#include "core/Error.h"
#include "core/codec/AppListCodec.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::service {

class FileService;

/// The icon of one app as the phone sends it.
struct AppIcon
{
    QString package;
    /// PNG data, at most codec::maxAppIconBytes; not decoded or checked yet.
    QByteArray png;
};

/**
 * The apps of the connected phone: the list, their icons, starting an app,
 * asking for its removal, copying its APK to this computer and handing an
 * APK to the phone for installation.
 *
 * Installing and removing need the user on the phone: the phone asks there,
 * and only a confirmation there changes the apps. The phone reports such a
 * change on its own while no OBEX session runs, which appAdded() and
 * appRemoved() pass on; while one runs, the phone drops the report.
 *
 * Copies run through the file service and its fast channel, one at a time
 * together with the copies of the file browser; the progress and the
 * cancellation of a copy are those of FileService.
 *
 * Every call that needs the phone fails with ErrorCode::ConnectionClosed when
 * it is not connected, and an unusable package name fails with
 * ErrorCode::InvalidInput before anything is sent. Error messages name no
 * app and no file. Lives in the main thread; no call blocks. Does not own the
 * connection and the file service, which must outlive it.
 */
class AppsService : public QObject
{
    Q_OBJECT

public:
    /// Side of the app icons asked for, in pixels.
    static constexpr int iconSize = 48;
    /// Most icons read in one OBEX session, so that other work on the phone is not held up long.
    static constexpr qsizetype maxIconsPerRequest = 40;

    AppsService(protocol::PhoneConnection *connection, FileService *files,
                QObject *parent = nullptr);

    /**
     * Reads every installed app. Reply lines that cannot be read are left out
     * and logged without their content. Fails with ErrorCode::Rejected or
     * ErrorCode::PermissionDenied when the phone refuses the list.
     */
    QFuture<Result<QList<codec::AppInfo>>> list();

    /**
     * Reads the icons of `packages`, at most maxIconsPerRequest, in one OBEX
     * session. An icon the phone cannot give, or one above
     * codec::maxAppIconBytes, is left out of the result.
     */
    QFuture<Result<QList<AppIcon>>> fetchIcons(const QStringList &packages);

    /**
     * Opens the app on the phone. Fails with ErrorCode::Rejected when the
     * phone has nothing to open for it, as for an app without a window.
     */
    QFuture<Result<void>> start(const QString &package);

    /**
     * Makes the phone ask its user whether to remove the app. Success means
     * only that the question is shown there; the app stays until the user
     * confirms it, and appRemoved() follows then.
     */
    QFuture<Result<void>> requestUninstall(const QString &package);

    /**
     * Copies the APK of `app` from its path on the phone into `localPath`,
     * as FileService::download() does. Fails with ErrorCode::NotSupported
     * when the phone gave no path or size for the APK.
     */
    QFuture<Result<void>> backup(const codec::AppInfo &app, const QString &localPath);

    /**
     * Hands the local APK `localPath` to the phone, which then asks its user
     * whether to install it; appAdded() follows a confirmation there. Fails
     * with ErrorCode::InvalidInput when the file name does not end in ".apk"
     * or cannot be used on the phone, when the content is no APK, which is a
     * ZIP archive, or when the file is empty or larger than
     * codec::maxApkBytes, and with ErrorCode::IoFailure when it cannot be read.
     */
    QFuture<Result<void>> install(const QString &localPath);

Q_SIGNALS:
    /// The phone reports an app as installed or updated.
    void appAdded(const qpm::core::codec::AppInfo &app);
    /// The phone reports the app `package` as removed.
    void appRemoved(const QString &package);

private:
    void onUnsolicited(const QString &line);

    protocol::PhoneConnection *m_connection = nullptr;
    FileService *m_files = nullptr;
};

} // namespace qpm::core::service
