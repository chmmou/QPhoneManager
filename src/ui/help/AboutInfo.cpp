#include "ui/help/AboutInfo.h"

#include "core/Logging.h"
#include "core/Version.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

namespace qpm::ui {

namespace {

QString resourceText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(core::lcUi) << "A license text is missing from the program:" << path;
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

// A line that starts a new line even inside a paragraph: a numbered clause, a
// bullet, a copyright notice or a web address.
bool startsOwnLine(const QString &line)
{
    static const QRegularExpression start(
        QStringLiteral("^(?:\\d+\\.\\s|[*-]\\s|Copyright\\b|https?://)"));
    return start.match(line).hasMatch();
}

} // namespace

QString reflowedLicenseText(const QString &text)
{
    QStringList lines;
    QString current;
    const auto finishLine = [&lines, &current]() {
        if (!current.isEmpty())
            lines.append(current);
        current.clear();
    };
    const QStringList input = text.split(u'\n');
    for (const QString &raw : input) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            finishLine();
            if (!lines.isEmpty() && !lines.constLast().isEmpty())
                lines.append(QString());
            continue;
        }
        if (current.isEmpty() || startsOwnLine(line)) {
            finishLine();
            current = line;
        } else {
            current += u' ' + line;
        }
    }
    finishLine();
    while (!lines.isEmpty() && lines.constLast().isEmpty())
        lines.removeLast();
    return lines.join(u'\n');
}

AboutInfo::AboutInfo(QObject *parent) : QAbstractListModel(parent)
{
    const QString ownLicense = resourceText(QStringLiteral(":/licenses/QPhoneManager.txt"));
    const QStringList lines = ownLicense.split(u'\n');
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1StringView("Copyright"))) {
            m_copyright = line.trimmed();
            break;
        }
    }
    // Name, version and license of each component follow THIRD_PARTY.md.
    m_components = {
        {QStringLiteral("QPhoneManager"), version(), license(), tr("This program."),
         reflowedLicenseText(ownLicense)},
        {QStringLiteral("Qt"), qtVersion(), QStringLiteral("LGPL-3.0-only"),
         tr("The framework QPhoneManager is built with."),
         // The copyright line is a legal notice and stays untranslated.
         QStringLiteral("Copyright (C) The Qt Company Ltd. and other contributors\n\n") +
             tr("QPhoneManager uses Qt under the GNU Lesser General Public License version 3 "
                "(LGPL-3.0-only) and links to it dynamically. You can replace the Qt libraries "
                "with "
                "your own build of the same Qt version.\n\n"
                "The source code of Qt is available at https://download.qt.io.\n\n"
                "The license text is at https://www.gnu.org/licenses/lgpl-3.0.html.")},
        {QStringLiteral("Tabler Icons"), QStringLiteral("3.51.0"), QStringLiteral("MIT"),
         tr("The icons of QPhoneManager."),
         reflowedLicenseText(resourceText(QStringLiteral(":/licenses/tabler-icons.txt")))},
        {QStringLiteral("QR Code generator library (Project Nayuki)"), QStringLiteral("1.8.0"),
         QStringLiteral("MIT"), tr("Draws the QR code for connecting over Wi-Fi."),
         reflowedLicenseText(resourceText(QStringLiteral(":/licenses/qrcodegen.txt")))},
    };
}

int AboutInfo::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_components.size());
}

QVariant AboutInfo::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Component &component = m_components.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return component.name;
    case VersionRole:
        return component.version;
    case LicenseRole:
        return component.license;
    case PurposeRole:
        return component.purpose;
    case LicenseTextRole:
        return component.licenseText;
    default:
        return {};
    }
}

QHash<int, QByteArray> AboutInfo::roleNames() const
{
    return {
        {NameRole, QByteArrayLiteral("name")},
        {VersionRole, QByteArrayLiteral("version")},
        {LicenseRole, QByteArrayLiteral("license")},
        {PurposeRole, QByteArrayLiteral("purpose")},
        {LicenseTextRole, QByteArrayLiteral("licenseText")},
    };
}

QString AboutInfo::version() const
{
    return applicationVersion();
}

QString AboutInfo::license() const
{
    return QStringLiteral("BSD-3-Clause");
}

QString AboutInfo::qtVersion() const
{
    return QString::fromLatin1(qVersion());
}

} // namespace qpm::ui
