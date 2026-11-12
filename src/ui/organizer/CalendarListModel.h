#pragma once

#include "core/codec/CalMetadata.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * The calendars of the phone, with the choice of synced calendars and of the
 * one for new events as the user edits it. The choice goes to the phone only
 * through CalendarArea::applyCalendarChoice(). Filled by CalendarArea.
 */
class CalendarListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CalendarListModel)
    QML_UNCREATABLE("Provided by CalendarArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// The choice differs from what the phone holds.
    Q_PROPERTY(bool modified READ isModified NOTIFY modifiedChanged FINAL)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        ColorRole,
        WritableRole,
        SelectedRole,
        DefaultRole,
        AccountRole,
    };

    explicit CalendarListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }
    [[nodiscard]] bool isModified() const { return m_edited != m_stored; }

    /// Replaces the calendars and drops an unsaved choice.
    void setCalendars(const QList<core::codec::CalendarInfo> &calendars);
    /// The calendars with the choice as edited.
    [[nodiscard]] QList<core::codec::CalendarInfo> edited() const { return m_edited; }
    /// The calendars as the phone holds them.
    [[nodiscard]] QList<core::codec::CalendarInfo> stored() const { return m_stored; }

    /// Whether the calendar in `row` is chosen for the sync.
    [[nodiscard]] Q_INVOKABLE bool isSelected(int row) const;
    /// Adds the calendar in `row` to the synced ones or takes it out.
    Q_INVOKABLE void setSelected(int row, bool selected);
    /// Makes the calendar in `row` the one for new events; it must be writable.
    Q_INVOKABLE bool setDefault(int row);
    /// Drops the edited choice.
    Q_INVOKABLE void revertChoice();

Q_SIGNALS:
    void countChanged();
    void modifiedChanged();

private:
    QList<core::codec::CalendarInfo> m_stored;
    QList<core::codec::CalendarInfo> m_edited;
};

} // namespace qpm::ui
