#pragma once

#include "ui/organizer/CalendarModel.h"

#include <QAbstractListModel>
#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * The days of the month or week CalendarArea shows, one row per day, each
 * with the first entries of that day for a grid cell. Filled by
 * CalendarArea from the same appearances as CalendarModel.
 */
class CalendarDaysModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CalendarDaysModel)
    QML_UNCREATABLE("Provided by CalendarArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// The day the user chose; its row shows that.
    Q_PROPERTY(QDate selectedDate READ selectedDate NOTIFY selectedDateChanged FINAL)

public:
    /// Entries a cell lists before it says how many more there are.
    static constexpr int entriesPerDay = 3;

    enum Role {
        DateRole = Qt::UserRole + 1,
        DayNumberRole,
        InPeriodRole,
        TodayRole,
        SelectedRole,
        HolidayRole,
        EntriesRole,
        EntryColorsRole,
        MoreCountRole,
        AccessibleTextRole,
        WeekdayRole,
    };

    explicit CalendarDaysModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }
    [[nodiscard]] QDate selectedDate() const { return m_selected; }

    /**
     * Shows the days from `first` to `last`; those outside the month
     * `periodMonth` (a date in it) look faded. Each entry of `occurrences`
     * appears on every day it covers; a holiday gives its day a name instead.
     */
    void setDays(const QDate &first, const QDate &last, const QDate &periodMonth,
                 const QList<Occurrence> &occurrences);
    void setSelectedDate(const QDate &date);

    /// The row of `date`, or -1.
    [[nodiscard]] Q_INVOKABLE int rowOf(const QDate &date) const;
    /// The date in `row`, or an invalid date.
    [[nodiscard]] Q_INVOKABLE QDate dateAt(int row) const;

Q_SIGNALS:
    void countChanged();
    void selectedDateChanged();

private:
    struct Day
    {
        QDate date;
        bool inPeriod = true;
        QString holiday;
        QStringList entries;
        QStringList colors;
        int total = 0;
    };

    QList<Day> m_days;
    QDate m_selected;
};

} // namespace qpm::ui
