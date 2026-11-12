#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <utility>

namespace qpm::ui {

/// One appearance of an event, or a public holiday, in the visible span.
struct Occurrence
{
    /// 0 for a holiday.
    quint32 luid = 0;
    QString summary;
    QString location;
    /// UTC; for an all-day entry midnight UTC of its first day.
    QDateTime start;
    QDateTime end;
    bool allDay = false;
    /// ARGB of the event, else of its calendar.
    quint32 color = 0;
    QString calendarTitle;
    bool recurring = false;
    bool holiday = false;
};

/**
 * The appearances of events in the span CalendarArea shows, repeating events
 * worked out, holidays included, sorted by start; all-day entries first on
 * their day. Narrowed by the search of the area. Filled by CalendarArea.
 */
class CalendarModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CalendarModel)
    QML_UNCREATABLE("Provided by CalendarArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        LuidRole = Qt::UserRole + 1,
        SummaryRole,
        LocationRole,
        StartRole,
        EndRole,
        AllDayRole,
        ColorRole,
        CalendarRole,
        TimeTextRole,
        DayTextRole,
        RecurringRole,
        HolidayRole,
        OccurrenceStartRole,
    };

    explicit CalendarModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }

    /// Replaces every entry; they are sorted here.
    void setOccurrences(QList<Occurrence> occurrences);
    /// Shows only entries whose title or place holds `text`, without regard to case.
    void setSearchText(const QString &text);
    [[nodiscard]] const QList<Occurrence> &shown() const { return m_shown; }

    /// "09:00 to 10:30", "All day" and the like, in local time.
    [[nodiscard]] static QString timeText(const Occurrence &occurrence);
    /**
     * The first and the last local day an entry covers; an end at midnight
     * belongs to the day before.
     */
    [[nodiscard]] static std::pair<QDate, QDate> daySpan(const Occurrence &occurrence);

    /// The row of the appearance of `luid` that starts at `startMs` (UTC), or -1.
    [[nodiscard]] Q_INVOKABLE int rowOf(int luid, qint64 startMs) const;
    /// The LUID of the event in `row`; 0 for a holiday or a row that does not exist.
    [[nodiscard]] Q_INVOKABLE int luidAt(int row) const;

Q_SIGNALS:
    void countChanged();

private:
    void refilter();

    QList<Occurrence> m_all;
    QList<Occurrence> m_shown;
    QString m_search;
};

} // namespace qpm::ui
