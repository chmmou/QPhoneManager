#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * Returns `text` with the lines of each paragraph joined, so that it wraps to
 * any width: blank lines stay as paragraph breaks, and a numbered clause, a
 * bullet, a copyright line and a web address keep their own line. The words
 * are not changed.
 */
[[nodiscard]] QString reflowedLicenseText(const QString &text);

/**
 * What the About dialog shows: the version of the program, its copyright and
 * license, the Qt version it runs on, and one row per component it is built
 * from, QPhoneManager itself first, each with version, license and its
 * license text: the full text where the program carries it, reflowed by
 * reflowedLicenseText(); for Qt its copyright, license and where to get its
 * source. The texts come from the resources of the program. Lives in the
 * main thread.
 */
class AboutInfo : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AboutInfo)
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    /// The copyright line of the license of QPhoneManager.
    Q_PROPERTY(QString copyright READ copyright CONSTANT FINAL)
    /// The SPDX identifier of the license of QPhoneManager.
    Q_PROPERTY(QString license READ license CONSTANT FINAL)
    /// The version of the Qt libraries loaded at run time.
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT FINAL)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        VersionRole,
        LicenseRole,
        /// One sentence on what the component is used for.
        PurposeRole,
        LicenseTextRole,
    };
    Q_ENUM(Role)

    explicit AboutInfo(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString version() const;
    [[nodiscard]] QString copyright() const { return m_copyright; }
    [[nodiscard]] QString license() const;
    [[nodiscard]] QString qtVersion() const;

private:
    struct Component
    {
        QString name;
        QString version;
        QString license;
        QString purpose;
        QString licenseText;
    };

    QList<Component> m_components;
    QString m_copyright;
};

} // namespace qpm::ui
