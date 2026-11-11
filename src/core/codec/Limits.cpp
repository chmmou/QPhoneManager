#include "core/codec/Limits.h"

#include <QDir>
#include <QFileInfo>

namespace qpm::core::limits {

namespace {

bool isAllowedNameCharacter(QChar c)
{
    return c.unicode() >= 0x20 && c.unicode() != 0x7F && c != u'/' && c != u'\\' &&
           c.category() != QChar::Other_Control && c.category() != QChar::Other_Format;
}

} // namespace

Result<QString> safeDownloadPath(const QString &targetDirectory, const QString &remoteName)
{
    if (remoteName.isEmpty() || remoteName == QLatin1StringView(".") ||
        remoteName == QLatin1StringView("..") || remoteName.toUtf8().size() > maxFileNameBytes) {
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("Unusable file name of %1 characters").arg(remoteName.size()));
    }
    for (const QChar c : remoteName) {
        if (!isAllowedNameCharacter(c)) {
            return Error(ErrorCode::InvalidInput,
                         QStringLiteral("File name contains the character U+%1")
                             .arg(uint(c.unicode()), 4, 16, QLatin1Char('0')));
        }
    }

    const QString base = QFileInfo(targetDirectory).canonicalFilePath();
    if (base.isEmpty() || !QFileInfo(base).isDir())
        return Error(ErrorCode::NotFound, QStringLiteral("Target directory does not exist"));

    const QString path = QDir(base).filePath(remoteName);
    const QFileInfo info(path);
    if (info.isSymLink() || (info.exists() && !info.isFile()))
        return Error(ErrorCode::InvalidInput,
                     QStringLiteral("Target exists and is not a regular file"));

    const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
    if (parent != base)
        return Error(ErrorCode::InvalidInput, QStringLiteral("Target lies outside the directory"));
    return path;
}

} // namespace qpm::core::limits
