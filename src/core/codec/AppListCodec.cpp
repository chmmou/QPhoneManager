#include "core/codec/AppListCodec.h"

#include "core/codec/AtText.h"
#include "core/codec/FolderListing.h"
#include "core/codec/PhoneTime.h"

#include <QRegularExpression>
#include <QSet>

namespace qpm::core::codec {

namespace {

constexpr int maxIconSize = 192;
constexpr QLatin1StringView linePrefix("*EJAVA:");
constexpr QLatin1StringView countPrefix("*EJAVACOUNT:");

Error malformed(const QString &what)
{
    return Error(ErrorCode::InvalidInput, QStringLiteral("App line: %1").arg(what));
}

bool isAsciiLetter(QChar c)
{
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

bool hasControlCharacter(QStringView text)
{
    for (const QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F)
            return true;
    }
    return false;
}

// A label or version is shown in the window; characters that change the
// writing direction could make it look like another app's.
bool isDirectionControl(QChar c)
{
    switch (c.direction()) {
    case QChar::DirLRE:
    case QChar::DirRLE:
    case QChar::DirLRO:
    case QChar::DirRLO:
    case QChar::DirPDF:
    case QChar::DirLRI:
    case QChar::DirRLI:
    case QChar::DirFSI:
    case QChar::DirPDI:
        return true;
    default:
        return false;
    }
}

QString shownText(const QString &text)
{
    QString result;
    result.reserve(text.size());
    for (const QChar c : text) {
        if (c.category() != QChar::Other_Control && !isDirectionControl(c))
            result += c;
    }
    return result.trimmed();
}

const QRegularExpression &linePattern()
{
    // The label is escaped and holds no quote; the version is not escaped and
    // may hold quotes and commas, so it ends at the last quote of the line.
    static const QRegularExpression pattern(
        QStringLiteral("^\"([^\"]*)\",\"\",\"(.*)\",([01]),([^,\"]*)"
                       "(?:,([^,\"]*),([^,\"]*),([^,\"]*))?$"));
    return pattern;
}

std::optional<quint64> parseApkSize(QStringView text)
{
    if (text.isEmpty() || text.size() > 20)
        return std::nullopt;
    for (const QChar c : text) {
        if (!c.isDigit() || c.unicode() > u'9')
            return std::nullopt;
    }
    bool ok = false;
    const qulonglong size = text.toULongLong(&ok);
    if (!ok || size > maxApkBytes)
        return std::nullopt;
    return size;
}

Result<QString> commandFor(const QLatin1StringView prefix, const QString &package)
{
    if (!isValidPackageName(package))
        return Error(ErrorCode::InvalidInput, QStringLiteral("Unusable package name"));
    return QString(prefix) + package;
}

} // namespace

bool isValidPackageName(QStringView package)
{
    if (package.isEmpty() || package.size() > maxPackageNameLength || !isAsciiLetter(package.at(0)))
        return false;
    bool segmentStart = true;
    for (const QChar c : package) {
        if (c == u'.') {
            if (segmentStart)
                return false;
            segmentStart = true;
            continue;
        }
        if (!isAsciiLetter(c) && !(c >= u'0' && c <= u'9') && c != u'_')
            return false;
        segmentStart = false;
    }
    return !segmentStart;
}

QString listAppsCommand()
{
    return QStringLiteral("AT*EJAVA=1");
}

Result<QString> startAppCommand(const QString &package)
{
    return commandFor(QLatin1StringView("AT*EJAVA=4,"), package);
}

Result<QString> uninstallAppCommand(const QString &package)
{
    return commandFor(QLatin1StringView("AT*EJAVA=2,"), package);
}

Result<QString> appIconName(const QString &package, int size)
{
    if (!isValidPackageName(package) || size < 1 || size > maxIconSize)
        return Error(ErrorCode::InvalidInput, QStringLiteral("Unusable package name or icon size"));
    return QStringLiteral("\\appicon\\%1\\%2").arg(package).arg(size);
}

Result<QString> apkPhonePath(const QString &sourcePath)
{
    const Error unusable(ErrorCode::InvalidInput, QStringLiteral("Unusable APK path"));
    if (sourcePath.size() > maxApkPathLength || !sourcePath.startsWith(u'/') ||
        !sourcePath.endsWith(QLatin1StringView(".apk"), Qt::CaseInsensitive)) {
        return unusable;
    }
    const QStringList parts = sourcePath.sliced(1).split(u'/');
    for (const QString &part : parts) {
        if (!isUsableFileName(part))
            return unusable;
    }
    return QStringLiteral("\\phone\\") + parts.join(u'\\');
}

Result<AppInfo> parseAppLine(const QString &line)
{
    if (!line.startsWith(linePrefix))
        return malformed(QStringLiteral("no *EJAVA prefix"));
    if (hasControlCharacter(line))
        return malformed(QStringLiteral("control character"));
    const QString rest = line.sliced(linePrefix.size()).trimmed();
    const QRegularExpressionMatch match = linePattern().match(rest);
    if (!match.hasMatch())
        return malformed(QStringLiteral("unknown form"));

    AppInfo app;
    app.package = decodeQp(match.captured(4));
    if (!isValidPackageName(app.package))
        return malformed(QStringLiteral("unusable package name"));

    app.label = shownText(decodeQp(match.captured(1)));
    if (app.label.size() > maxAppLabelLength)
        return malformed(QStringLiteral("label too long"));
    if (app.label.isEmpty())
        app.label = app.package;

    const QString version = match.captured(2);
    if (version.size() > maxAppVersionLength)
        return malformed(QStringLiteral("version too long"));
    // The phone writes a missing version name as the text "null".
    app.version = version == QLatin1StringView("null") ? QString() : shownText(version);
    app.isUserApp = match.capturedView(3) == u"1";

    if (match.hasCaptured(5)) {
        app.sourcePath = decodeQp(match.captured(5));
        if (!apkPhonePath(app.sourcePath).hasValue())
            return malformed(QStringLiteral("unusable APK path"));
        const std::optional<QDateTime> installed = parseUtcStamp(match.capturedView(6));
        if (!installed)
            return malformed(QStringLiteral("impossible time"));
        app.installed = *installed;
        app.size = parseApkSize(match.capturedView(7));
        if (!app.size)
            return malformed(QStringLiteral("size out of range"));
    }
    return app;
}

Result<AppList> parseAppList(const QStringList &lines)
{
    const auto tooMany = [](qsizetype count) {
        return Error(
            ErrorCode::LimitExceeded,
            QStringLiteral("The phone lists %1 apps, at most %2 are read").arg(count).arg(maxApps));
    };
    AppList list;
    QSet<QString> seen;
    for (const QString &line : lines) {
        if (line.startsWith(countPrefix)) {
            bool ok = false;
            const qlonglong announced = line.sliced(countPrefix.size()).trimmed().toLongLong(&ok);
            if (ok && announced > maxApps)
                return tooMany(qsizetype(announced));
            continue;
        }
        if (list.apps.size() + list.malformed >= maxApps)
            return tooMany(lines.size());
        const Result<AppInfo> app = parseAppLine(line);
        if (!app.hasValue() || seen.contains(app.value().package)) {
            ++list.malformed;
            continue;
        }
        seen.insert(app.value().package);
        list.apps.append(app.value());
    }
    return list;
}

Result<AppEvent> parseAppEvent(const QString &line)
{
    static const QRegularExpression removedPattern(
        QStringLiteral("^\\*EJAVA:\\s*\"\",\"\",\"\",0,([^,\"]+)$"));
    const QRegularExpressionMatch removed = removedPattern.match(line);
    if (removed.hasMatch()) {
        AppEvent event;
        event.kind = AppEvent::Kind::Removed;
        event.app.package = decodeQp(removed.captured(1));
        if (!isValidPackageName(event.app.package))
            return malformed(QStringLiteral("unusable package name"));
        return event;
    }
    const Result<AppInfo> added = parseAppLine(line);
    if (!added.hasValue())
        return added.error();
    AppEvent event;
    event.kind = AppEvent::Kind::Added;
    event.app = added.value();
    return event;
}

} // namespace qpm::core::codec
