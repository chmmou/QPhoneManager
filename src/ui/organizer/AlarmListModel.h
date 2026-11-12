#pragma once

#include "core/service/AlarmService.h"

#include <QAbstractListModel>
#include <QList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/// The alarms of the clock app on the phone, in its order. Filled by CalendarArea.
class AlarmListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AlarmListModel)
    QML_UNCREATABLE("Provided by CalendarArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        AlarmIdRole = Qt::UserRole + 1,
        TimeTextRole,
        NameRole,
        DaysTextRole,
        EnabledRole,
    };

    explicit AlarmListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }

    void setAlarms(const QList<core::service::PhoneAlarm> &alarms);
    /// The alarm in `row`; a new one for a row that does not exist.
    [[nodiscard]] core::service::PhoneAlarm at(int row) const;

    /// "Once", "Every day", "Monday, Tuesday" and the like.
    [[nodiscard]] static QString daysText(const QList<int> &days);

Q_SIGNALS:
    void countChanged();

private:
    QList<core::service::PhoneAlarm> m_alarms;
};

} // namespace qpm::ui
