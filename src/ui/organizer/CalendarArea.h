#pragma once

#include "core/Error.h"
#include "core/Holidays.h"
#include "core/codec/PhoneVCal.h"
#include "core/store/OrganizerStore.h"
#include "ui/organizer/AlarmListModel.h"
#include "ui/organizer/CalendarDaysModel.h"
#include "ui/organizer/CalendarListModel.h"
#include "ui/organizer/CalendarModel.h"
#include "ui/organizer/EventEditor.h"

#include <QColor>
#include <QDate>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::service {
class AlarmService;
class CalendarService;
} // namespace qpm::core::service

namespace qpm::core::store {
class ProfileStore;
}

namespace qpm::ui {

/**
 * The calendar area of the window: a month, week, day or list view of the
 * events with repeating events worked out and public holidays added, the
 * event shown, the form, the choice of synced calendars, iCalendar import
 * and export, and the alarms of the phone. Every failure is reported once
 * through errorOccurred() as a translated text that says what to do.
 *
 * Owned by AppController, which hands in the profile store of the current
 * phone, the connection state and the holiday country. The events are shown
 * without a connection; changing them needs one. No call blocks; files are
 * read and written on a worker thread.
 */
class CalendarArea : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CalendarArea)
    QML_UNCREATABLE("Provided by AppController")

    /// The appearances of the chosen day, or of the month in the list view.
    Q_PROPERTY(qpm::ui::CalendarModel *list READ list CONSTANT FINAL)
    /// The cells of the month or week view; empty in the day and list views.
    Q_PROPERTY(qpm::ui::CalendarDaysModel *days READ days CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::CalendarListModel *calendars READ calendars CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::EventEditor *editor READ editor CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::AlarmListModel *alarms READ alarms CONSTANT FINAL)
    Q_PROPERTY(ViewMode viewMode READ viewMode WRITE setViewMode NOTIFY viewChanged FINAL)
    /// viewMode as the position of its tab: month, week, day, list.
    Q_PROPERTY(int viewIndex READ viewIndex WRITE setViewIndex NOTIFY viewChanged FINAL)
    /// The day the views are built around; it is the chosen day.
    Q_PROPERTY(QDate currentDate READ currentDate WRITE setCurrentDate NOTIFY viewChanged FINAL)
    /// "November 2026", the week or the day the view shows.
    Q_PROPERTY(QString periodTitle READ periodTitle NOTIFY viewChanged FINAL)
    /// The long name of the chosen day.
    Q_PROPERTY(QString currentDateText READ currentDateText NOTIFY viewChanged FINAL)
    /// Narrows the list to entries whose title or place holds it.
    Q_PROPERTY(
        QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged FINAL)
    Q_PROPERTY(bool hasPhone READ hasPhone NOTIFY hasPhoneChanged FINAL)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY canEditChanged FINAL)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// The event shown; 0 for none.
    Q_PROPERTY(int currentLuid READ currentLuid NOTIFY currentChanged FINAL)
    /// The row of the appearance shown in the list; -1 when the list does not show it.
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentSummary READ currentSummary NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentWhen READ currentWhen NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentLocation READ currentLocation NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentDescription READ currentDescription NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentCalendar READ currentCalendar NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentRepeat READ currentRepeat NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentReminders READ currentReminders NOTIFY currentChanged FINAL)
    Q_PROPERTY(QColor currentColor READ currentColor NOTIFY currentChanged FINAL)
    /// The event shown belongs to a series, so one appearance can be left out.
    Q_PROPERTY(bool currentRecurring READ currentRecurring NOTIFY currentChanged FINAL)
    /// The phone may change the event shown.
    Q_PROPERTY(bool currentWritable READ currentWritable NOTIFY currentChanged FINAL)
    Q_PROPERTY(AlarmState alarmState READ alarmState NOTIFY alarmStateChanged FINAL)

public:
    enum class ViewMode { Month, Week, Day, List };
    Q_ENUM(ViewMode)

    enum class AlarmState { NotLoaded, Loading, Ready, NotSupported, Failed };
    Q_ENUM(AlarmState)

    CalendarArea(core::protocol::PhoneConnection *connection, QObject *parent = nullptr);

    [[nodiscard]] CalendarModel *list() const { return m_list; }
    [[nodiscard]] CalendarDaysModel *days() const { return m_days; }
    [[nodiscard]] CalendarListModel *calendars() const { return m_calendars; }
    [[nodiscard]] EventEditor *editor() const { return m_editor; }
    [[nodiscard]] AlarmListModel *alarms() const { return m_alarms; }
    [[nodiscard]] core::service::CalendarService *service() const { return m_service; }
    [[nodiscard]] ViewMode viewMode() const { return m_viewMode; }
    void setViewMode(ViewMode mode);
    [[nodiscard]] int viewIndex() const { return static_cast<int>(m_viewMode); }
    void setViewIndex(int index);
    [[nodiscard]] QDate currentDate() const { return m_currentDate; }
    void setCurrentDate(const QDate &date);
    [[nodiscard]] QString periodTitle() const;
    [[nodiscard]] QString currentDateText() const;
    [[nodiscard]] QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    [[nodiscard]] bool hasPhone() const { return m_store != nullptr; }
    [[nodiscard]] bool canEdit() const { return m_connected && m_store; }
    [[nodiscard]] bool isBusy() const { return m_busy > 0; }
    [[nodiscard]] int currentLuid() const { return m_current ? int(m_current->luid) : 0; }
    [[nodiscard]] int currentRow() const;
    [[nodiscard]] QString currentSummary() const;
    [[nodiscard]] QString currentWhen() const;
    [[nodiscard]] QString currentLocation() const;
    [[nodiscard]] QString currentDescription() const;
    [[nodiscard]] QString currentCalendar() const;
    [[nodiscard]] QString currentRepeat() const;
    [[nodiscard]] QString currentReminders() const;
    [[nodiscard]] QColor currentColor() const;
    [[nodiscard]] bool currentRecurring() const { return m_current && !m_current->rrule.isEmpty(); }
    [[nodiscard]] bool currentWritable() const;
    [[nodiscard]] AlarmState alarmState() const { return m_alarmState; }

    /**
     * Shows the events of `profile`; null shows none. The store must stay
     * open until this is called again with another one or null.
     */
    void setProfileStore(core::store::ProfileStore *profile);
    /// The store of the events and tasks, shared with the tasks area; null without a phone.
    [[nodiscard]] std::shared_ptr<core::store::OrganizerStore> store() const { return m_store; }
    void setConnected(bool connected);
    /// "de", "at", "ch" or empty, as Settings::holidayRegion holds it.
    void setHolidayRegion(const QString &region);

    /// Pulls what changed in the calendar and the tasks, for the sync of all areas.
    QFuture<core::Result<core::store::OrganizerChanges>> sync();

    /// The span before the one shown.
    Q_INVOKABLE void previous();
    /// The span after the one shown.
    Q_INVOKABLE void next();
    Q_INVOKABLE void goToToday();
    /// Shows the event `luid` in its appearance that starts at `startMs` (UTC); 0 shows none.
    Q_INVOKABLE void selectEvent(int luid, qint64 startMs);
    /// Shows the event in `row` of `list`; a holiday shows none.
    Q_INVOKABLE void selectRow(int row);
    /// The title of the stored event `luid`; empty when it is unknown.
    [[nodiscard]] Q_INVOKABLE QString summaryOf(int luid) const;
    /// Deletes the event and the whole series it belongs to, on the phone and here.
    Q_INVOKABLE bool removeEvent(int luid);
    /// Leaves out the appearance of the series shown, on the phone and here.
    Q_INVOKABLE bool skipCurrentOccurrence();
    /**
     * Reads the iCalendar file `url` and creates its events in the calendar
     * for new events and its tasks on the phone.
     */
    Q_INVOKABLE bool importFrom(const QUrl &url);
    /// Writes every event and task to the iCalendar file `url`.
    Q_INVOKABLE bool exportTo(const QUrl &url);
    /// Sends the choice of synced calendars made in `calendars` to the phone.
    Q_INVOKABLE bool applyCalendarChoice();
    /// Reads the alarms of the phone; alarmState follows.
    Q_INVOKABLE bool loadAlarms();
    /**
     * Saves the alarm in `row` of `alarms`, or a new one for -1, with the
     * time `timeText`, the `name`, the weekdays `days` (1 Monday to 7 Sunday,
     * empty rings once) and `enabled`. False and an error for a time that
     * cannot be read.
     */
    Q_INVOKABLE bool saveAlarm(int row, const QString &timeText, const QString &name,
                               const QList<int> &days, bool enabled);
    /// The time of the alarm in `row` of `alarms`, as the form takes it.
    [[nodiscard]] Q_INVOKABLE QString alarmTimeText(int row) const;
    [[nodiscard]] Q_INVOKABLE QString alarmName(int row) const;
    /// The weekdays of the alarm in `row`, 1 Monday to 7 Sunday.
    [[nodiscard]] Q_INVOKABLE QList<int> alarmDays(int row) const;
    [[nodiscard]] Q_INVOKABLE bool alarmEnabled(int row) const;

Q_SIGNALS:
    void viewChanged();
    void searchTextChanged();
    void hasPhoneChanged();
    void canEditChanged();
    void busyChanged();
    void currentChanged();
    void alarmStateChanged();
    void errorOccurred(const QString &message);
    /// A finished action the user should hear about, for example an import.
    void infoMessage(const QString &message);

private:
    void reload();
    void rebuild();
    void showCurrent();
    void report(const core::Error &error);
    void beginWork();
    void endWork();
    void setAlarmState(AlarmState state);
    [[nodiscard]] std::optional<core::codec::CalendarInfo>
    calendarOf(const core::codec::CalendarEvent &event) const;
    [[nodiscard]] QList<Occurrence> occurrencesBetween(const QDate &first, const QDate &last) const;

    core::service::CalendarService *m_service = nullptr;
    core::service::AlarmService *m_alarmService = nullptr;
    std::shared_ptr<core::store::OrganizerStore> m_store;
    CalendarModel *m_list = nullptr;
    CalendarDaysModel *m_days = nullptr;
    CalendarListModel *m_calendars = nullptr;
    EventEditor *m_editor = nullptr;
    AlarmListModel *m_alarms = nullptr;
    QList<core::codec::CalendarInfo> m_calendarInfo;
    std::optional<core::codec::CalendarEvent> m_current;
    /// UTC start of the appearance shown.
    QDateTime m_currentStart;
    QDate m_currentDate;
    QString m_searchText;
    core::HolidayRegion m_holidays = core::HolidayRegion::None;
    ViewMode m_viewMode = ViewMode::Month;
    AlarmState m_alarmState = AlarmState::NotLoaded;
    int m_busy = 0;
    bool m_connected = false;
};

} // namespace qpm::ui
