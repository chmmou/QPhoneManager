#include "ui/organizer/CalendarArea.h"

#include "core/codec/ICalendar.h"
#include "core/codec/Recurrence.h"
#include "core/service/AlarmService.h"
#include "core/service/CalendarService.h"
#include "core/store/ProfileStore.h"
#include "ui/organizer/OrganizerText.h"

#include <QFile>
#include <QLocale>
#include <QSaveFile>
#include <QTimeZone>
#include <QtConcurrentRun>

#include <algorithm>

namespace qpm::ui {

namespace {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::codec::CalendarEvent;
using core::codec::CalendarInfo;

constexpr quint32 fallbackColor = 0xFF006A6A;
constexpr quint32 holidayColor = 0xFFB3261E;
constexpr int gridDays = 7 * 6;
constexpr int daysPerWeek = 7;
constexpr qsizetype maxStartsPerSeries = 1000;
constexpr qint64 secondsPerMinute = 60;

int daysIntoWeek(const QDate &date)
{
    return (date.dayOfWeek() - int(QLocale().firstDayOfWeek()) + daysPerWeek) % daysPerWeek;
}

core::HolidayRegion regionFrom(const QString &region)
{
    if (region == QLatin1StringView("de"))
        return core::HolidayRegion::Germany;
    if (region == QLatin1StringView("at"))
        return core::HolidayRegion::Austria;
    if (region == QLatin1StringView("ch"))
        return core::HolidayRegion::Switzerland;
    return core::HolidayRegion::None;
}

Result<core::codec::ICalendarContent> readCalendarFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot open the file"));
    if (file.size() > core::codec::maxICalendarBytes) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("File of %1 bytes is too large").arg(file.size()));
    }
    return core::codec::readICalendar(file.read(core::codec::maxICalendarBytes + 1));
}

Result<int> writeCalendarFile(const QString &path, const QList<CalendarEvent> &events,
                              const QList<core::codec::CalendarTask> &tasks)
{
    const QByteArray data = core::codec::writeICalendar(events, tasks);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot write the file"));
    return int(events.size() + tasks.size());
}

} // namespace

CalendarArea::CalendarArea(core::protocol::PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_service(new core::service::CalendarService(connection, this)),
      m_alarmService(new core::service::AlarmService(connection, this)),
      m_list(new CalendarModel(this)), m_days(new CalendarDaysModel(this)),
      m_calendars(new CalendarListModel(this)), m_editor(new EventEditor(m_service, this)),
      m_alarms(new AlarmListModel(this)), m_currentDate(QDate::currentDate())
{
    connect(m_service, &core::service::CalendarService::calendarChanged, this,
            &CalendarArea::reload);
    connect(m_editor, &EventEditor::errorOccurred, this, &CalendarArea::errorOccurred);
    connect(m_editor, &EventEditor::saved, this, [this](int luid) {
        const Result<CalendarEvent> event =
            m_store ? m_store->event(quint32(luid)) : Result<CalendarEvent>(Error());
        if (event.hasValue())
            selectEvent(luid, event.value().start.toMSecsSinceEpoch());
    });
    rebuild();
}

void CalendarArea::setViewMode(ViewMode mode)
{
    if (mode == m_viewMode)
        return;
    m_viewMode = mode;
    rebuild();
    Q_EMIT viewChanged();
}

void CalendarArea::setViewIndex(int index)
{
    if (index >= static_cast<int>(ViewMode::Month) && index <= static_cast<int>(ViewMode::List))
        setViewMode(static_cast<ViewMode>(index));
}

void CalendarArea::setCurrentDate(const QDate &date)
{
    if (!date.isValid() || date == m_currentDate)
        return;
    m_currentDate = date;
    rebuild();
    Q_EMIT viewChanged();
}

QString CalendarArea::periodTitle() const
{
    const QLocale locale;
    switch (m_viewMode) {
    case ViewMode::Week: {
        const QDate first = m_currentDate.addDays(-daysIntoWeek(m_currentDate));
        return tr("Week %1: %2 to %3")
            .arg(first.weekNumber())
            .arg(OrganizerText::formatDate(first),
                 OrganizerText::formatDate(first.addDays(daysPerWeek - 1)));
    }
    case ViewMode::Day:
        return locale.toString(m_currentDate, QLocale::LongFormat);
    case ViewMode::Month:
    case ViewMode::List:
        break;
    }
    return locale.standaloneMonthName(m_currentDate.month()) + u' ' +
           QString::number(m_currentDate.year());
}

QString CalendarArea::currentDateText() const
{
    return QLocale().toString(m_currentDate, QLocale::LongFormat);
}

void CalendarArea::setSearchText(const QString &text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    m_list->setSearchText(text);
    Q_EMIT searchTextChanged();
}

QString CalendarArea::currentSummary() const
{
    return m_current ? m_current->summary : QString();
}

QString CalendarArea::currentWhen() const
{
    if (!m_current)
        return {};
    Occurrence occurrence;
    occurrence.allDay = m_current->allDay;
    occurrence.start = m_currentStart.isValid() ? m_currentStart : m_current->start;
    occurrence.end = occurrence.start.addSecs(m_current->start.secsTo(m_current->end));
    const QDate day = occurrence.allDay ? occurrence.start.toUTC().date()
                                        : occurrence.start.toLocalTime().date();
    return tr("%1, %2").arg(QLocale().toString(day, QLocale::LongFormat),
                            CalendarModel::timeText(occurrence));
}

QString CalendarArea::currentLocation() const
{
    return m_current ? m_current->location : QString();
}

QString CalendarArea::currentDescription() const
{
    return m_current ? m_current->description : QString();
}

QString CalendarArea::currentCalendar() const
{
    return m_current ? m_current->calendarTitle : QString();
}

QString CalendarArea::currentRepeat() const
{
    return m_current ? OrganizerText::repeatText(m_current->rrule) : QString();
}

QString CalendarArea::currentReminders() const
{
    if (!m_current)
        return {};
    const QDateTime base = m_current->allDay
                               ? QDateTime(m_current->start.toUTC().date(), QTime(0, 0)).toUTC()
                               : m_current->start;
    QStringList texts;
    for (const QDateTime &alarm : m_current->alarms)
        texts.append(OrganizerText::reminderText(alarm.secsTo(base) / secondsPerMinute));
    return texts.join(QLatin1StringView(", "));
}

QColor CalendarArea::currentColor() const
{
    if (!m_current)
        return {};
    if (m_current->color)
        return QColor::fromRgb(*m_current->color | 0xFF000000U);
    const std::optional<CalendarInfo> calendar = calendarOf(*m_current);
    return QColor::fromRgb((calendar ? calendar->color : fallbackColor) | 0xFF000000U);
}

bool CalendarArea::currentWritable() const
{
    if (!m_current)
        return false;
    const std::optional<CalendarInfo> calendar = calendarOf(*m_current);
    return !calendar || calendar->isWritable();
}

void CalendarArea::setProfileStore(core::store::ProfileStore *profile)
{
    m_store = profile ? std::make_shared<core::store::OrganizerStore>(profile) : nullptr;
    m_service->setStore(m_store);
    m_editor->setStore(m_store);
    m_editor->setConnected(canEdit());
    m_current.reset();
    m_alarms->setAlarms({});
    setAlarmState(AlarmState::NotLoaded);
    reload();
    Q_EMIT currentChanged();
    Q_EMIT hasPhoneChanged();
    Q_EMIT canEditChanged();
}

void CalendarArea::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    m_editor->setConnected(canEdit());
    if (!connected && m_alarmState == AlarmState::Loading)
        setAlarmState(AlarmState::NotLoaded);
    Q_EMIT canEditChanged();
}

void CalendarArea::setHolidayRegion(const QString &region)
{
    const core::HolidayRegion holidays = regionFrom(region);
    if (holidays == m_holidays)
        return;
    m_holidays = holidays;
    rebuild();
}

QFuture<Result<core::store::OrganizerChanges>> CalendarArea::sync()
{
    return m_service->sync();
}

void CalendarArea::previous()
{
    switch (m_viewMode) {
    case ViewMode::Month:
    case ViewMode::List:
        setCurrentDate(m_currentDate.addMonths(-1));
        return;
    case ViewMode::Week:
        setCurrentDate(m_currentDate.addDays(-daysPerWeek));
        return;
    case ViewMode::Day:
        break;
    }
    setCurrentDate(m_currentDate.addDays(-1));
}

void CalendarArea::next()
{
    switch (m_viewMode) {
    case ViewMode::Month:
    case ViewMode::List:
        setCurrentDate(m_currentDate.addMonths(1));
        return;
    case ViewMode::Week:
        setCurrentDate(m_currentDate.addDays(daysPerWeek));
        return;
    case ViewMode::Day:
        break;
    }
    setCurrentDate(m_currentDate.addDays(1));
}

void CalendarArea::goToToday()
{
    setCurrentDate(QDate::currentDate());
}

void CalendarArea::selectEvent(int luid, qint64 startMs)
{
    if (luid <= 0 || !m_store) {
        if (!m_current)
            return;
        m_current.reset();
        Q_EMIT currentChanged();
        return;
    }
    const Result<CalendarEvent> event = m_store->event(quint32(luid));
    if (!event.hasValue()) {
        report(event.error());
        return;
    }
    m_current = event.value();
    m_currentStart = QDateTime::fromMSecsSinceEpoch(startMs, QTimeZone::UTC);
    Q_EMIT currentChanged();
}

int CalendarArea::currentRow() const
{
    if (!m_current)
        return -1;
    const int luid = int(m_current->luid);
    const int exact = m_list->rowOf(luid, m_currentStart.toMSecsSinceEpoch());
    if (exact >= 0 || !m_current->rrule.isEmpty())
        return exact;
    // A single event chosen without its start appears at most once.
    for (int row = 0; row < m_list->rowCount(); ++row) {
        if (m_list->luidAt(row) == luid)
            return row;
    }
    return -1;
}

void CalendarArea::selectRow(int row)
{
    const QList<Occurrence> &shown = m_list->shown();
    if (row < 0 || row >= shown.size())
        return;
    selectEvent(int(shown.at(row).luid), shown.at(row).start.toMSecsSinceEpoch());
}

QString CalendarArea::summaryOf(int luid) const
{
    const Result<CalendarEvent> event =
        m_store && luid > 0 ? m_store->event(quint32(luid)) : Result<CalendarEvent>(Error());
    return event.hasValue() ? event.value().summary : QString();
}

std::optional<CalendarInfo> CalendarArea::calendarOf(const CalendarEvent &event) const
{
    for (const CalendarInfo &calendar : m_calendarInfo) {
        if (calendar.url == event.calendarUrl && calendar.title == event.calendarTitle)
            return calendar;
    }
    return std::nullopt;
}

QList<Occurrence> CalendarArea::occurrencesBetween(const QDate &first, const QDate &last) const
{
    QList<Occurrence> occurrences;
    const QDateTime from = QDateTime(first, QTime(0, 0)).toUTC();
    const QDateTime to = QDateTime(last.addDays(1), QTime(0, 0)).toUTC();
    if (m_store) {
        const Result<QList<CalendarEvent>> events = m_store->eventsBetween(
            from.addDays(-1), to.addDays(1));
        const QList<CalendarEvent> found = events.hasValue() ? events.value() : QList<CalendarEvent>();
        for (const CalendarEvent &event : found) {
            const std::optional<CalendarInfo> calendar = calendarOf(event);
            Occurrence base;
            base.luid = event.luid;
            base.summary = event.summary;
            base.location = event.location;
            base.allDay = event.allDay;
            base.color = event.color ? *event.color : calendar ? calendar->color : fallbackColor;
            base.calendarTitle = event.calendarTitle;
            base.recurring = !event.rrule.isEmpty();
            const qint64 duration = std::max<qint64>(0, event.start.secsTo(event.end));
            QList<QDateTime> starts;
            const Result<core::codec::RecurrenceRule> rule =
                event.rrule.isEmpty() ? Result<core::codec::RecurrenceRule>(
                                            Error(ErrorCode::InvalidInput, QString()))
                                      : core::codec::parseRecurrenceRule(event.rrule);
            if (rule.hasValue()) {
                const QByteArray zoneId = calendar ? calendar->timezone.toUtf8() : QByteArray();
                const QTimeZone zone = QTimeZone::isTimeZoneIdAvailable(zoneId)
                                           ? QTimeZone(zoneId)
                                           : QTimeZone::systemTimeZone();
                const QDateTime spanFrom =
                    event.allDay ? QDateTime(first, QTime(0, 0), QTimeZone::UTC) : from;
                const QDateTime spanTo =
                    event.allDay ? QDateTime(last.addDays(1), QTime(0, 0), QTimeZone::UTC) : to;
                starts = core::codec::expandRecurrence(event.start, event.allDay, rule.value(),
                                                       event.exdates, spanFrom.addSecs(-duration),
                                                       spanTo, zone, maxStartsPerSeries);
            } else {
                starts = {event.start};
            }
            for (const QDateTime &start : std::as_const(starts)) {
                Occurrence occurrence = base;
                occurrence.start = start;
                occurrence.end = start.addSecs(duration);
                const auto [firstDay, lastDay] = CalendarModel::daySpan(occurrence);
                if (lastDay >= first && firstDay <= last)
                    occurrences.append(occurrence);
            }
        }
    }
    const QList<core::Holiday> holidays = core::holidaysBetween(m_holidays, first, last);
    for (const core::Holiday &holiday : holidays) {
        Occurrence occurrence;
        occurrence.summary = OrganizerText::holidayName(holiday.kind);
        occurrence.allDay = true;
        occurrence.start = QDateTime(holiday.date, QTime(0, 0), QTimeZone::UTC);
        occurrence.end = occurrence.start.addDays(1);
        occurrence.color = holidayColor;
        occurrence.holiday = true;
        occurrences.append(occurrence);
    }
    return occurrences;
}

void CalendarArea::rebuild()
{
    const QDate monthFirst(m_currentDate.year(), m_currentDate.month(), 1);
    const QDate monthLast = monthFirst.addMonths(1).addDays(-1);
    QDate first = m_currentDate;
    QDate last = m_currentDate;
    switch (m_viewMode) {
    case ViewMode::Month:
        first = monthFirst.addDays(-daysIntoWeek(monthFirst));
        last = first.addDays(gridDays - 1);
        break;
    case ViewMode::Week:
        first = m_currentDate.addDays(-daysIntoWeek(m_currentDate));
        last = first.addDays(daysPerWeek - 1);
        break;
    case ViewMode::List:
        first = monthFirst;
        last = monthLast;
        break;
    case ViewMode::Day:
        break;
    }
    const QList<Occurrence> occurrences = occurrencesBetween(first, last);
    if (m_viewMode == ViewMode::Month || m_viewMode == ViewMode::Week) {
        m_days->setDays(first, last, m_viewMode == ViewMode::Month ? m_currentDate : QDate(),
                        occurrences);
    } else {
        m_days->setDays({}, {}, {}, {});
    }
    m_days->setSelectedDate(m_currentDate);
    if (m_viewMode == ViewMode::List) {
        m_list->setOccurrences(occurrences);
        return;
    }
    QList<Occurrence> ofDay;
    for (const Occurrence &occurrence : occurrences) {
        const auto [firstDay, lastDay] = CalendarModel::daySpan(occurrence);
        if (firstDay <= m_currentDate && lastDay >= m_currentDate)
            ofDay.append(occurrence);
    }
    m_list->setOccurrences(ofDay);
}

void CalendarArea::reload()
{
    m_calendarInfo.clear();
    if (m_store) {
        const Result<core::codec::CalMetadata> metadata = m_store->calendarMetadata();
        if (metadata.hasValue())
            m_calendarInfo = metadata.value().calendars;
        else
            report(metadata.error());
    }
    m_calendars->setCalendars(m_calendarInfo);
    m_editor->setStore(m_store);
    rebuild();
    showCurrent();
}

void CalendarArea::showCurrent()
{
    if (!m_current)
        return;
    const Result<CalendarEvent> event =
        m_store ? m_store->event(m_current->luid) : Result<CalendarEvent>(Error());
    if (event.hasValue())
        m_current = event.value();
    else
        m_current.reset();
    Q_EMIT currentChanged();
}

void CalendarArea::report(const Error &error)
{
    const QString message = OrganizerText::errorText(error, OrganizerText::Area::Calendar);
    if (!message.isEmpty())
        Q_EMIT errorOccurred(message);
}

void CalendarArea::beginWork()
{
    if (m_busy++ == 0)
        Q_EMIT busyChanged();
}

void CalendarArea::endWork()
{
    if (m_busy > 0 && --m_busy == 0)
        Q_EMIT busyChanged();
}

bool CalendarArea::removeEvent(int luid)
{
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    if (luid <= 0)
        return false;
    beginWork();
    m_service->remove({quint32(luid)}).then(this, [this, luid](const Result<void> &removed) {
        endWork();
        if (!removed.hasValue()) {
            report(removed.error());
            return;
        }
        if (m_current && int(m_current->luid) == luid) {
            m_current.reset();
            Q_EMIT currentChanged();
        }
    });
    return true;
}

bool CalendarArea::skipCurrentOccurrence()
{
    if (!m_current || m_current->rrule.isEmpty() || !m_currentStart.isValid())
        return false;
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    CalendarEvent event = *m_current;
    event.exdates.append(m_currentStart);
    beginWork();
    m_service->saveEvent(event, core::service::SaveMode::CheckConflict)
        .then(this, [this](const Result<core::service::EventSaveOutcome> &saved) {
            endWork();
            if (!saved.hasValue()) {
                report(saved.error());
                return;
            }
            if (saved.value().conflict) {
                Q_EMIT errorOccurred(tr("The appointment was changed on the phone. Sync, then try "
                                        "again."));
                return;
            }
            m_current.reset();
            Q_EMIT currentChanged();
            Q_EMIT infoMessage(tr("This appointment is left out of the series."));
        });
    return true;
}

bool CalendarArea::importFrom(const QUrl &url)
{
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    std::optional<CalendarInfo> target;
    for (const CalendarInfo &calendar : std::as_const(m_calendarInfo)) {
        if (calendar.isWritable() && (!target || (calendar.isDefault && !target->isDefault)))
            target = calendar;
    }
    const QString path = url.toLocalFile();
    beginWork();
    QtConcurrent::run([path]() {
        return readCalendarFile(path);
    }).then(this, [this, target](const Result<core::codec::ICalendarContent> &read) {
        if (!read.hasValue()) {
            endWork();
            Q_EMIT errorOccurred(read.error().code() == ErrorCode::IoFailure
                                     ? tr("The file cannot be opened. Check that it exists.")
                                     : tr("The file holds no appointments or tasks QPhoneManager "
                                          "can read. Choose an iCalendar file."));
            return;
        }
        if (read.value().events.isEmpty() && read.value().tasks.isEmpty()) {
            endWork();
            Q_EMIT errorOccurred(tr("The file holds no appointments or tasks."));
            return;
        }
        if (!read.value().events.isEmpty() && !target) {
            endWork();
            Q_EMIT errorOccurred(tr("The phone has no calendar the app may write to. Add one on "
                                    "the phone, then try again."));
            return;
        }
        const int unreadable = read.value().skipped;
        m_service->importItems(read.value().events, read.value().tasks, target.value_or(CalendarInfo()))
            .then(this, [this, unreadable](
                            const Result<core::service::OrganizerImportOutcome> &imported) {
                endWork();
                if (!imported.hasValue()) {
                    report(imported.error());
                    return;
                }
                QString message =
                    tr("%n entry(s) imported.", nullptr, imported.value().created);
                const int skipped = imported.value().skipped + unreadable;
                if (skipped > 0) {
                    message += u' ' + tr("%n entry(s) were left out, because they lack a title or "
                                         "a start.",
                                         nullptr, skipped);
                }
                Q_EMIT infoMessage(message);
            });
    });
    return true;
}

bool CalendarArea::exportTo(const QUrl &url)
{
    if (!m_store) {
        report(Error(ErrorCode::InvalidInput, QStringLiteral("No phone chosen")));
        return false;
    }
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    const Result<QList<CalendarEvent>> events = m_store->allEvents();
    const Result<QList<core::codec::CalendarTask>> tasks = m_store->tasks();
    if (!events.hasValue() || !tasks.hasValue()) {
        report(events.hasValue() ? tasks.error() : events.error());
        return false;
    }
    const QString path = url.toLocalFile();
    const QList<CalendarEvent> eventList = events.value();
    const QList<core::codec::CalendarTask> taskList = tasks.value();
    beginWork();
    QtConcurrent::run([path, eventList, taskList]() {
        return writeCalendarFile(path, eventList, taskList);
    }).then(this, [this](const Result<int> &written) {
        endWork();
        if (!written.hasValue()) {
            Q_EMIT errorOccurred(tr("The file cannot be written. Choose another folder."));
            return;
        }
        Q_EMIT infoMessage(tr("%n entry(s) exported.", nullptr, written.value()));
    });
    return true;
}

bool CalendarArea::applyCalendarChoice()
{
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    if (!m_calendars->isModified())
        return true;
    beginWork();
    m_service->setCalendarSelection(m_calendars->edited()).then(this, [this](const Result<void> &set) {
        endWork();
        if (!set.hasValue()) {
            report(set.error());
            return;
        }
        Q_EMIT infoMessage(tr("The phone now syncs the calendars you chose."));
    });
    return true;
}

void CalendarArea::setAlarmState(AlarmState state)
{
    if (state == m_alarmState)
        return;
    m_alarmState = state;
    Q_EMIT alarmStateChanged();
}

bool CalendarArea::loadAlarms()
{
    if (!m_connected) {
        Q_EMIT errorOccurred(OrganizerText::errorText(
            Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")),
            OrganizerText::Area::Alarms));
        return false;
    }
    setAlarmState(AlarmState::Loading);
    m_alarmService->alarms().then(
        this, [this](const Result<QList<core::service::PhoneAlarm>> &read) {
            if (read.hasValue()) {
                m_alarms->setAlarms(read.value());
                setAlarmState(AlarmState::Ready);
                return;
            }
            m_alarms->setAlarms({});
            if (read.error().code() == ErrorCode::NotSupported) {
                setAlarmState(AlarmState::NotSupported);
                return;
            }
            setAlarmState(AlarmState::Failed);
            const QString message =
                OrganizerText::errorText(read.error(), OrganizerText::Area::Alarms);
            if (!message.isEmpty())
                Q_EMIT errorOccurred(message);
        });
    return true;
}

bool CalendarArea::saveAlarm(int row, const QString &timeText, const QString &name,
                             const QList<int> &days, bool enabled)
{
    const QTime time = OrganizerText::parseTime(timeText);
    if (!time.isValid()) {
        Q_EMIT errorOccurred(OrganizerText::errorText(
            Error(ErrorCode::InvalidInput, QStringLiteral("No time")), OrganizerText::Area::Alarms));
        return false;
    }
    if (!m_connected) {
        Q_EMIT errorOccurred(OrganizerText::errorText(
            Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")),
            OrganizerText::Area::Alarms));
        return false;
    }
    core::service::PhoneAlarm alarm = row >= 0 ? m_alarms->at(row) : core::service::PhoneAlarm();
    alarm.time = time;
    alarm.name = name.trimmed();
    alarm.days = days;
    std::sort(alarm.days.begin(), alarm.days.end());
    alarm.enabled = enabled;
    beginWork();
    m_alarmService->save(alarm).then(this, [this](const Result<void> &saved) {
        endWork();
        if (!saved.hasValue()) {
            if (saved.error().code() == ErrorCode::NotSupported)
                setAlarmState(AlarmState::NotSupported);
            const QString message =
                OrganizerText::errorText(saved.error(), OrganizerText::Area::Alarms);
            if (!message.isEmpty())
                Q_EMIT errorOccurred(message);
            return;
        }
        Q_EMIT infoMessage(tr("The alarm is saved on the phone."));
        static_cast<void>(loadAlarms());
    });
    return true;
}

QString CalendarArea::alarmTimeText(int row) const
{
    return OrganizerText::formatTime(m_alarms->at(row).time);
}

QString CalendarArea::alarmName(int row) const
{
    return m_alarms->at(row).name;
}

QList<int> CalendarArea::alarmDays(int row) const
{
    return m_alarms->at(row).days;
}

bool CalendarArea::alarmEnabled(int row) const
{
    return m_alarms->at(row).enabled;
}

} // namespace qpm::ui
