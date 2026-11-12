#include "ui/organizer/EventEditor.h"

#include "core/store/OrganizerStore.h"
#include "ui/organizer/CalendarModel.h"
#include "ui/organizer/OrganizerText.h"

#include <QTimeZone>

#include <algorithm>

namespace qpm::ui {

namespace {

using core::codec::CalendarEvent;
using core::codec::CalendarInfo;

constexpr int defaultStartHour = 9;
constexpr int standardRepeatChoices = 5;
constexpr qint64 secondsPerMinute = 60;

} // namespace

EventEditor::EventEditor(core::service::CalendarService *service, QObject *parent)
    : QObject(parent), m_service(service)
{
    Q_ASSERT(service);
    newEvent(QDate());
}

void EventEditor::setStore(const std::shared_ptr<core::store::OrganizerStore> &store)
{
    m_store = store;
    reloadCalendars();
    Q_EMIT canSaveChanged();
}

void EventEditor::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    Q_EMIT canSaveChanged();
}

void EventEditor::changed()
{
    Q_EMIT fieldsChanged();
    Q_EMIT canSaveChanged();
}

void EventEditor::setSummary(const QString &value)
{
    if (value == m_event.summary)
        return;
    m_event.summary = value;
    changed();
}

void EventEditor::setLocation(const QString &value)
{
    if (value == m_event.location)
        return;
    m_event.location = value;
    changed();
}

void EventEditor::setDescription(const QString &value)
{
    if (value == m_event.description)
        return;
    m_event.description = value;
    changed();
}

void EventEditor::setAllDay(bool allDay)
{
    if (allDay == m_event.allDay)
        return;
    m_event.allDay = allDay;
    if (!allDay && OrganizerText::parseTime(m_startTime).isNull()) {
        m_startTime = OrganizerText::formatTime(QTime(defaultStartHour, 0));
        m_endTime = OrganizerText::formatTime(QTime(defaultStartHour + 1, 0));
    }
    changed();
}

void EventEditor::setStartDate(const QString &value)
{
    if (value == m_startDate)
        return;
    // A later start keeps the length of the event, as calendars do.
    const QDate before = OrganizerText::parseDate(m_startDate);
    const QDate after = OrganizerText::parseDate(value);
    const QDate end = OrganizerText::parseDate(m_endDate);
    m_startDate = value;
    if (before.isValid() && after.isValid() && end.isValid())
        m_endDate = OrganizerText::formatDate(end.addDays(before.daysTo(after)));
    changed();
}

void EventEditor::setStartTime(const QString &value)
{
    if (value == m_startTime)
        return;
    m_startTime = value;
    changed();
}

void EventEditor::setEndDate(const QString &value)
{
    if (value == m_endDate)
        return;
    m_endDate = value;
    changed();
}

void EventEditor::setEndTime(const QString &value)
{
    if (value == m_endTime)
        return;
    m_endTime = value;
    changed();
}

QStringList EventEditor::repeatChoices() const
{
    QStringList choices = OrganizerText::repeatChoices();
    if (!m_customRule.isEmpty())
        choices.append(OrganizerText::repeatText(m_customRule));
    return choices;
}

void EventEditor::setRepeatIndex(int index)
{
    if (index == m_repeatIndex || index < 0 || index >= repeatChoices().size())
        return;
    m_repeatIndex = index;
    changed();
}

QStringList EventEditor::reminders() const
{
    QStringList texts;
    for (const qint64 minutes : m_reminderMinutes)
        texts.append(OrganizerText::reminderText(minutes));
    return texts;
}

QStringList EventEditor::reminderChoices() const
{
    return OrganizerText::reminderChoices();
}

QStringList EventEditor::visibilityChoices() const
{
    return OrganizerText::visibilityChoices();
}

void EventEditor::setVisibilityIndex(int index)
{
    if (index < 0 || index >= visibilityChoices().size() || index == visibilityIndex())
        return;
    m_event.visibility = static_cast<core::codec::EventVisibility>(index);
    changed();
}

QStringList EventEditor::availabilityChoices() const
{
    return OrganizerText::availabilityChoices();
}

void EventEditor::setAvailabilityIndex(int index)
{
    if (index < 0 || index >= availabilityChoices().size() || index == availabilityIndex())
        return;
    m_event.availability = static_cast<core::codec::EventAvailability>(index);
    changed();
}

QStringList EventEditor::colorChoices() const
{
    return OrganizerText::colorChoices();
}

void EventEditor::setColorIndex(int index)
{
    if (index < 0 || index >= colorChoices().size() || index == m_colorIndex)
        return;
    m_colorIndex = index;
    changed();
}

QStringList EventEditor::calendarNames() const
{
    QStringList names;
    for (const CalendarInfo &calendar : m_calendars)
        names.append(calendar.title);
    return names;
}

void EventEditor::setCalendarIndex(int index)
{
    if (index < 0 || index >= m_calendars.size() || index == m_calendarIndex)
        return;
    m_calendarIndex = index;
    changed();
}

QString EventEditor::calendarText() const
{
    return m_original.calendarTitle;
}

QString EventEditor::dateExample() const
{
    return OrganizerText::dateExample();
}

QString EventEditor::timeExample() const
{
    return OrganizerText::timeExample();
}

QString EventEditor::dateHint() const
{
    return OrganizerText::dateHint();
}

QString EventEditor::timeHint() const
{
    return OrganizerText::timeHint();
}

QDateTime EventEditor::startOf() const
{
    const QDate date = OrganizerText::parseDate(m_startDate);
    if (!date.isValid())
        return {};
    if (m_event.allDay)
        return QDateTime(date, QTime(0, 0), QTimeZone::UTC);
    const QTime time = OrganizerText::parseTime(m_startTime);
    return time.isValid() ? QDateTime(date, time).toUTC() : QDateTime();
}

QDateTime EventEditor::endOf() const
{
    const QDate date = OrganizerText::parseDate(m_endDate);
    if (!date.isValid())
        return {};
    if (m_event.allDay)
        return QDateTime(date.addDays(1), QTime(0, 0), QTimeZone::UTC);
    const QTime time = OrganizerText::parseTime(m_endTime);
    return time.isValid() ? QDateTime(date, time).toUTC() : QDateTime();
}

QDateTime EventEditor::reminderBase(const CalendarEvent &event) const
{
    if (event.allDay)
        return QDateTime(event.start.toUTC().date(), QTime(0, 0)).toUTC();
    return event.start;
}

QString EventEditor::problem() const
{
    if (m_event.summary.trimmed().isEmpty())
        return tr("Enter a title.");
    if (!OrganizerText::parseDate(m_startDate).isValid())
        return tr("Enter the start date like %1.").arg(dateExample());
    if (!m_event.allDay && !OrganizerText::parseTime(m_startTime).isValid())
        return tr("Enter the start time like %1.").arg(timeExample());
    if (!OrganizerText::parseDate(m_endDate).isValid())
        return tr("Enter the end date like %1.").arg(dateExample());
    if (!m_event.allDay && !OrganizerText::parseTime(m_endTime).isValid())
        return tr("Enter the end time like %1.").arg(timeExample());
    if (endOf() < startOf())
        return tr("The end lies before the start.");
    if (isNew() && m_calendars.isEmpty())
        return tr("The phone has no calendar the app may write to.");
    return {};
}

CalendarEvent EventEditor::edited() const
{
    CalendarEvent event = m_event;
    event.start = startOf();
    event.end = endOf();
    event.rrule = m_repeatIndex >= standardRepeatChoices ? m_customRule
                                                         : OrganizerText::repeatRule(m_repeatIndex);
    if (event.rrule.isEmpty())
        event.exdates.clear();
    event.color = OrganizerText::colorValue(m_colorIndex);
    if (event.color && m_original.color &&
        OrganizerText::colorIndex(m_original.color) == m_colorIndex) {
        // An unchanged choice keeps the exact colour of the phone.
        event.color = m_original.color;
    }
    if (isNew() && m_calendarIndex >= 0 && m_calendarIndex < m_calendars.size()) {
        event.calendarUrl = m_calendars.at(m_calendarIndex).url;
        event.calendarTitle = m_calendars.at(m_calendarIndex).title;
    }
    event.alarms.clear();
    if (event.start.isValid()) {
        const QDateTime base = reminderBase(event);
        for (const qint64 minutes : m_reminderMinutes)
            event.alarms.append(base.addSecs(-minutes * secondsPerMinute));
    }
    return event;
}

bool EventEditor::isModified() const
{
    return isNew() || !core::codec::sameEventContent(edited(), m_original);
}

bool EventEditor::canSave() const
{
    return m_connected && !m_busy && !m_store.expired() && problem().isEmpty();
}

QString EventEditor::summaryText(const CalendarEvent &event)
{
    QStringList lines;
    lines.append(event.summary);
    Occurrence occurrence;
    occurrence.start = event.start;
    occurrence.end = event.end;
    occurrence.allDay = event.allDay;
    const QDate day = event.allDay ? event.start.toUTC().date() : event.start.toLocalTime().date();
    lines.append(tr("%1, %2").arg(OrganizerText::formatDate(day), CalendarModel::timeText(occurrence)));
    if (!event.location.trimmed().isEmpty())
        lines.append(tr("Place: %1").arg(event.location.trimmed()));
    const QString repeat = OrganizerText::repeatText(event.rrule);
    if (!repeat.isEmpty())
        lines.append(repeat);
    for (const QDateTime &alarm : event.alarms) {
        const QDateTime local = alarm.toLocalTime();
        lines.append(tr("Reminder: %1 %2").arg(OrganizerText::formatDate(local.date()),
                                               OrganizerText::formatTime(local.time())));
    }
    if (!event.description.trimmed().isEmpty())
        lines.append(event.description.trimmed());
    return lines.join(u'\n');
}

QString EventEditor::ownVersionText() const
{
    return m_conflict ? summaryText(edited()) : QString();
}

QString EventEditor::phoneVersionText() const
{
    return m_conflict ? summaryText(m_conflict->phoneVersion) : QString();
}

void EventEditor::reloadCalendars()
{
    m_calendars.clear();
    if (const std::shared_ptr<core::store::OrganizerStore> store = m_store.lock()) {
        const core::Result<core::codec::CalMetadata> metadata = store->calendarMetadata();
        if (metadata.hasValue()) {
            for (const CalendarInfo &calendar : metadata.value().calendars) {
                if (calendar.isWritable())
                    m_calendars.append(calendar);
            }
        }
    }
    std::stable_sort(m_calendars.begin(), m_calendars.end(),
                     [](const CalendarInfo &a, const CalendarInfo &b) {
                         return a.isDefault && !b.isDefault;
                     });
    if (m_calendarIndex >= m_calendars.size())
        m_calendarIndex = 0;
    Q_EMIT calendarsChanged();
}

void EventEditor::show(const CalendarEvent &event)
{
    m_original = event;
    m_event = event;
    if (event.allDay) {
        const QDate first = event.start.toUTC().date();
        const QDate last = event.end.toUTC().date().addDays(-1);
        m_startDate = OrganizerText::formatDate(first);
        m_endDate = OrganizerText::formatDate(last < first ? first : last);
        m_startTime = OrganizerText::formatTime(QTime(defaultStartHour, 0));
        m_endTime = OrganizerText::formatTime(QTime(defaultStartHour + 1, 0));
    } else {
        const QDateTime start = event.start.toLocalTime();
        const QDateTime end = event.end.toLocalTime();
        m_startDate = OrganizerText::formatDate(start.date());
        m_startTime = OrganizerText::formatTime(start.time());
        m_endDate = OrganizerText::formatDate(end.date());
        m_endTime = OrganizerText::formatTime(end.time());
    }
    const std::optional<int> repeat = OrganizerText::repeatIndex(event.rrule);
    m_customRule = repeat ? QString() : event.rrule;
    m_repeatIndex = repeat ? *repeat : standardRepeatChoices;
    m_colorIndex = OrganizerText::colorIndex(event.color);
    m_reminderMinutes.clear();
    const QDateTime base = reminderBase(event);
    for (const QDateTime &alarm : event.alarms)
        m_reminderMinutes.append(alarm.secsTo(base) / secondsPerMinute);
    m_calendarIndex = 0;
    const bool hadConflict = m_conflict.has_value();
    m_conflict.reset();
    Q_EMIT loaded();
    changed();
    if (hadConflict)
        Q_EMIT conflictChanged();
}

void EventEditor::newEvent(const QDate &day)
{
    const QDate date = day.isValid() ? day : QDate::currentDate();
    CalendarEvent event;
    event.start = QDateTime(date, QTime(defaultStartHour, 0)).toUTC();
    event.end = event.start.addSecs(3600);
    reloadCalendars();
    show(event);
}

bool EventEditor::load(int luid)
{
    const std::shared_ptr<core::store::OrganizerStore> store = m_store.lock();
    const core::Result<CalendarEvent> event =
        store && luid > 0 ? store->event(quint32(luid))
                          : core::Result<CalendarEvent>(
                                core::Error(core::ErrorCode::NotFound, QStringLiteral("No event")));
    if (!event.hasValue()) {
        Q_EMIT errorOccurred(OrganizerText::errorText(event.error(), OrganizerText::Area::Calendar));
        return false;
    }
    reloadCalendars();
    show(event.value());
    return true;
}

void EventEditor::addReminder(int choice)
{
    const QList<int> offsets = OrganizerText::reminderOffsets();
    if (choice < 0 || choice >= offsets.size() || m_reminderMinutes.contains(offsets.at(choice)))
        return;
    m_reminderMinutes.append(offsets.at(choice));
    std::sort(m_reminderMinutes.begin(), m_reminderMinutes.end());
    changed();
}

void EventEditor::removeReminder(int index)
{
    if (index < 0 || index >= m_reminderMinutes.size())
        return;
    m_reminderMinutes.removeAt(index);
    changed();
}

void EventEditor::clearExceptions()
{
    if (m_event.exdates.isEmpty())
        return;
    m_event.exdates.clear();
    changed();
}

void EventEditor::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
    Q_EMIT canSaveChanged();
}

bool EventEditor::save()
{
    if (m_busy)
        return false;
    const QString why = problem();
    if (!why.isEmpty()) {
        Q_EMIT errorOccurred(why);
        return false;
    }
    if (!m_connected) {
        Q_EMIT errorOccurred(OrganizerText::errorText(
            core::Error(core::ErrorCode::ConnectionClosed, QStringLiteral("Not connected")),
            OrganizerText::Area::Calendar));
        return false;
    }
    startSave(core::service::SaveMode::CheckConflict);
    return true;
}

void EventEditor::startSave(core::service::SaveMode mode)
{
    setBusy(true);
    m_service->saveEvent(edited(), mode)
        .then(this, [this](const core::Result<core::service::EventSaveOutcome> &outcome) {
            setBusy(false);
            if (!outcome.hasValue()) {
                const QString message =
                    OrganizerText::errorText(outcome.error(), OrganizerText::Area::Calendar);
                if (!message.isEmpty())
                    Q_EMIT errorOccurred(message);
                return;
            }
            if (outcome.value().conflict) {
                m_conflict = outcome.value();
                Q_EMIT conflictChanged();
                return;
            }
            const int luid = int(outcome.value().luid);
            static_cast<void>(load(luid));
            Q_EMIT saved(luid);
        });
}

bool EventEditor::keepOwnVersion()
{
    if (!m_conflict || m_busy)
        return false;
    m_conflict.reset();
    Q_EMIT conflictChanged();
    startSave(core::service::SaveMode::Overwrite);
    return true;
}

bool EventEditor::takePhoneVersion()
{
    if (!m_conflict || m_busy)
        return false;
    const core::Result<void> kept = m_service->keepPhoneVersion(m_conflict->phoneObject);
    if (!kept.hasValue()) {
        Q_EMIT errorOccurred(OrganizerText::errorText(kept.error(), OrganizerText::Area::Calendar));
        return false;
    }
    const int luid = int(m_original.luid);
    static_cast<void>(load(luid));
    Q_EMIT saved(luid);
    return true;
}

} // namespace qpm::ui
