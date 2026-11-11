#pragma once

#include "core/store/ProfileStore.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * The phones known on this computer, sorted by name, with the current one
 * marked. Filled by AppController.
 */
class ProfileListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ProfileListModel)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        DeviceIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        ModelRole,
        LastConnectionRole,
        CurrentRole,
    };

    explicit ProfileListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return rowCount(); }

    void setProfiles(QList<core::store::DeviceProfile> profiles, const QString &currentId);

    /// The row of `deviceId`, or -1.
    [[nodiscard]] Q_INVOKABLE int indexOf(const QString &deviceId) const;

    [[nodiscard]] const QList<core::store::DeviceProfile> &profiles() const { return m_profiles; }

Q_SIGNALS:
    void countChanged();

private:
    QList<core::store::DeviceProfile> m_profiles;
    QString m_currentId;
};

} // namespace qpm::ui
