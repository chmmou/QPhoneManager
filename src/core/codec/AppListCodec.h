#pragma once

#include "core/Error.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace qpm::core::codec {

/// One installed app as the phone lists it.
struct AppInfo
{
    /// The Android package name; isValidPackageName() holds for it.
    QString package;
    /// The name the phone shows for the app; the package name when the phone gives none.
    QString label;
    /// The version name; empty when the app has none.
    QString version;
    /// Installed by the user rather than with the system.
    bool isUserApp = false;
    /**
     * Absolute path of the APK on the phone, such as
     * "/data/app/~~a1==/com.example-b2==/base.apk"; empty when the phone
     * does not give it.
     */
    QString sourcePath;
    /**
     * When the APK file on the phone was last written, in UTC: the time of
     * the installation or of the last update. Invalid when not known.
     */
    QDateTime installed;
    /// Size of the APK in bytes; unset when not known.
    std::optional<quint64> size;

    bool operator==(const AppInfo &other) const = default;
};

/// The apps of one list and the number of reply lines that could not be read.
struct AppList
{
    QList<AppInfo> apps;
    int malformed = 0;
};

/// What a line the phone sends on its own reports about its apps.
struct AppEvent
{
    enum class Kind { Added, Removed };
    Kind kind = Kind::Added;
    /// The whole app for Added; only the package for Removed.
    AppInfo app;
};

/// Most apps read from one list.
inline constexpr qsizetype maxApps = 5000;
/// Longest package name, label, version and APK path taken from the phone, in characters.
inline constexpr qsizetype maxPackageNameLength = 255;
inline constexpr qsizetype maxAppLabelLength = 256;
inline constexpr qsizetype maxAppVersionLength = 128;
inline constexpr qsizetype maxApkPathLength = 1024;
/// Largest APK size taken from the phone or sent to it.
inline constexpr quint64 maxApkBytes = 16ULL * 1024 * 1024 * 1024;
/// Largest app icon read from the phone.
inline constexpr quint64 maxAppIconBytes = 1024 * 1024;

/**
 * Returns whether `package` is a usable Android package name: 1 to
 * maxPackageNameLength characters, segments of ASCII letters, digits and
 * underscores split by single dots, the first segment starting with a
 * letter. Such a name needs no escaping on the AT channel.
 */
[[nodiscard]] bool isValidPackageName(QStringView package);

/// "AT*EJAVA=1": lists every installed app.
[[nodiscard]] QString listAppsCommand();

/// "AT*EJAVA=4,<package>"; fails with ErrorCode::InvalidInput for an unusable package name.
[[nodiscard]] Result<QString> startAppCommand(const QString &package);

/**
 * "AT*EJAVA=2,<package>": makes the phone ask its user whether to remove the
 * app. Fails with ErrorCode::InvalidInput for an unusable package name.
 */
[[nodiscard]] Result<QString> uninstallAppCommand(const QString &package);

/**
 * The name to read the icon of `package` with on the file browser target:
 * "\appicon\<package>\<size>". `size` is the side in pixels, 1 to 192.
 * Fails with ErrorCode::InvalidInput for an unusable package name or size.
 */
[[nodiscard]] Result<QString> appIconName(const QString &package, int size);

/**
 * The file browser path of the APK at `sourcePath` on the phone: "\phone"
 * followed by the path with backslashes. Fails with ErrorCode::InvalidInput
 * when `sourcePath` is not an absolute path to an ".apk" file whose parts
 * codec::isUsableFileName() accepts.
 */
[[nodiscard]] Result<QString> apkPhonePath(const QString &sourcePath);

/**
 * Reads one line of the app list or one full line the phone sends after an
 * installation:
 * *EJAVA: "<label>","","<version>",<1|0>,<package>[,<path>,<yyyyMMddTHHmmssZ>,<bytes>]
 *
 * Label, package and path are decoded with decodeQp(); the version comes
 * unescaped, so the line is taken apart from both ends, and the text "null"
 * stands for no version. Control characters and the characters that change
 * the writing direction are dropped from label and version. Fails with
 * ErrorCode::InvalidInput for any other form, an unusable package name, a
 * path apkPhonePath() refuses, an impossible time, and values above the
 * limits of this file.
 */
[[nodiscard]] Result<AppInfo> parseAppLine(const QString &line);

/**
 * Reads the reply lines of AT*EJAVA=1. The leading "*EJAVACOUNT:" line is
 * skipped; lines that cannot be read and second lines of the same package are
 * left out and counted. Fails with ErrorCode::LimitExceeded when the phone
 * announces or sends more than maxApps apps.
 */
[[nodiscard]] Result<AppList> parseAppList(const QStringList &lines);

/**
 * Reads an "*EJAVA:" line the phone sends on its own: a full line after an
 * installation, or `*EJAVA: "","","",0,<package>` after a removal. Fails with
 * ErrorCode::InvalidInput for anything else.
 */
[[nodiscard]] Result<AppEvent> parseAppEvent(const QString &line);

} // namespace qpm::core::codec
