#pragma once

#include "core/codec/CalMetadata.h"
#include "core/codec/PhoneVCal.h"
#include "core/service/CalendarService.h"

#include <QDate>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

namespace qpm::core::store {
class OrganizerStore;
}

namespace qpm::ui {

/**
 * The form for one appointment, new or loaded from the store, with its
 * fields as properties. Dates and times are typed as text in the form of the
 * locale; problem says what keeps them from being used. Reminders are kept as
 * minutes before the start, for an all-day event before midnight of its first
 * day.
 *
 * save() sends the whole event to the phone. When the event changed on the
 * phone since it was loaded, the save stops, hasConflict turns true and both
 * versions are shown; keepOwnVersion() or takePhoneVersion() decides. A new
 * event can only go into a calendar the phone may write to; the calendar of
 * an existing event cannot change.
 *
 * Owned by CalendarArea, which hands in the store and the connection state.
 * Saving needs a connection; everything else works without one.
 */
class EventEditor : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(EventEditor)
    QML_UNCREATABLE("Provided by CalendarArea")

    Q_PROPERTY(bool isNew READ isNew NOTIFY loaded FINAL)
    Q_PROPERTY(int luid READ luid NOTIFY loaded FINAL)
    Q_PROPERTY(QString summary READ summary WRITE setSummary NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString location READ location WRITE setLocation NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(
        QString description READ description WRITE setDescription NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(bool allDay READ allDay WRITE setAllDay NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString startDate READ startDate WRITE setStartDate NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString startTime READ startTime WRITE setStartTime NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString endDate READ endDate WRITE setEndDate NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString endTime READ endTime WRITE setEndTime NOTIFY fieldsChanged FINAL)
    /// The repetitions offered; a rule of the phone the form cannot build comes last.
    Q_PROPERTY(QStringList repeatChoices READ repeatChoices NOTIFY loaded FINAL)
    Q_PROPERTY(int repeatIndex READ repeatIndex WRITE setRepeatIndex NOTIFY fieldsChanged FINAL)
    /// Appearances of the series left out on the phone.
    Q_PROPERTY(int exceptionCount READ exceptionCount NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QStringList reminders READ reminders NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QStringList reminderChoices READ reminderChoices CONSTANT FINAL)
    Q_PROPERTY(QStringList visibilityChoices READ visibilityChoices CONSTANT FINAL)
    Q_PROPERTY(int visibilityIndex READ visibilityIndex WRITE setVisibilityIndex NOTIFY
                   fieldsChanged FINAL)
    Q_PROPERTY(QStringList availabilityChoices READ availabilityChoices CONSTANT FINAL)
    Q_PROPERTY(int availabilityIndex READ availabilityIndex WRITE setAvailabilityIndex NOTIFY
                   fieldsChanged FINAL)
    Q_PROPERTY(QStringList colorChoices READ colorChoices CONSTANT FINAL)
    Q_PROPERTY(int colorIndex READ colorIndex WRITE setColorIndex NOTIFY fieldsChanged FINAL)
    /// Calendars a new event can go to, the phone's default first.
    Q_PROPERTY(QStringList calendarNames READ calendarNames NOTIFY calendarsChanged FINAL)
    Q_PROPERTY(
        int calendarIndex READ calendarIndex WRITE setCalendarIndex NOTIFY fieldsChanged FINAL)
    /// The calendar of a loaded event.
    Q_PROPERTY(QString calendarText READ calendarText NOTIFY loaded FINAL)
    /// Why the form cannot be saved as it is; empty when it can.
    Q_PROPERTY(QString problem READ problem NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString dateExample READ dateExample CONSTANT FINAL)
    Q_PROPERTY(QString timeExample READ timeExample CONSTANT FINAL)
    Q_PROPERTY(bool modified READ isModified NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// Connected, not busy, no problem.
    Q_PROPERTY(bool canSave READ canSave NOTIFY canSaveChanged FINAL)
    Q_PROPERTY(bool hasConflict READ hasConflict NOTIFY conflictChanged FINAL)
    Q_PROPERTY(QString ownVersionText READ ownVersionText NOTIFY conflictChanged FINAL)
    Q_PROPERTY(QString phoneVersionText READ phoneVersionText NOTIFY conflictChanged FINAL)

public:
    /// `service` must outlive this object.
    explicit EventEditor(core::service::CalendarService *service, QObject *parent = nullptr);

    void setStore(const std::shared_ptr<core::store::OrganizerStore> &store);
    void setConnected(bool connected);

    [[nodiscard]] bool isNew() const { return m_original.luid == 0; }
    [[nodiscard]] int luid() const { return int(m_original.luid); }
    [[nodiscard]] QString summary() const { return m_event.summary; }
    void setSummary(const QString &value);
    [[nodiscard]] QString location() const { return m_event.location; }
    void setLocation(const QString &value);
    [[nodiscard]] QString description() const { return m_event.description; }
    void setDescription(const QString &value);
    [[nodiscard]] bool allDay() const { return m_event.allDay; }
    void setAllDay(bool allDay);
    [[nodiscard]] QString startDate() const { return m_startDate; }
    void setStartDate(const QString &value);
    [[nodiscard]] QString startTime() const { return m_startTime; }
    void setStartTime(const QString &value);
    [[nodiscard]] QString endDate() const { return m_endDate; }
    void setEndDate(const QString &value);
    [[nodiscard]] QString endTime() const { return m_endTime; }
    void setEndTime(const QString &value);
    [[nodiscard]] QStringList repeatChoices() const;
    [[nodiscard]] int repeatIndex() const { return m_repeatIndex; }
    void setRepeatIndex(int index);
    [[nodiscard]] int exceptionCount() const { return int(m_event.exdates.size()); }
    [[nodiscard]] QStringList reminders() const;
    [[nodiscard]] QStringList reminderChoices() const;
    [[nodiscard]] QStringList visibilityChoices() const;
    [[nodiscard]] int visibilityIndex() const { return static_cast<int>(m_event.visibility); }
    void setVisibilityIndex(int index);
    [[nodiscard]] QStringList availabilityChoices() const;
    [[nodiscard]] int availabilityIndex() const { return static_cast<int>(m_event.availability); }
    void setAvailabilityIndex(int index);
    [[nodiscard]] QStringList colorChoices() const;
    [[nodiscard]] int colorIndex() const { return m_colorIndex; }
    void setColorIndex(int index);
    [[nodiscard]] QStringList calendarNames() const;
    [[nodiscard]] int calendarIndex() const { return m_calendarIndex; }
    void setCalendarIndex(int index);
    [[nodiscard]] QString calendarText() const;
    [[nodiscard]] QString problem() const;
    [[nodiscard]] QString dateExample() const;
    [[nodiscard]] QString timeExample() const;
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] bool isBusy() const { return m_busy; }
    [[nodiscard]] bool canSave() const;
    [[nodiscard]] bool hasConflict() const { return m_conflict.has_value(); }
    [[nodiscard]] QString ownVersionText() const;
    [[nodiscard]] QString phoneVersionText() const;

    /// Empties the form for a new event on `day`, from 9 to 10 o'clock; today when invalid.
    Q_INVOKABLE void newEvent(const QDate &day);
    /// Fills the form with the stored event `luid`; false and an error when it is unknown.
    Q_INVOKABLE bool load(int luid);
    /// Starts saving; false and an error when the form cannot be saved now.
    Q_INVOKABLE bool save();
    /// After a conflict: writes this form over the phone's version.
    Q_INVOKABLE bool keepOwnVersion();
    /// After a conflict: drops this form and keeps the phone's version.
    Q_INVOKABLE bool takePhoneVersion();
    /// Adds the reminder at `choice` of reminderChoices; one already there is not added twice.
    Q_INVOKABLE void addReminder(int choice);
    Q_INVOKABLE void removeReminder(int index);
    /// Brings back every left-out appearance of the series.
    Q_INVOKABLE void clearExceptions();

    /// The event as the form holds it; the start is invalid while problem() says why.
    [[nodiscard]] core::codec::CalendarEvent edited() const;

    /// A few lines with the fields of `event`, for comparing two versions.
    [[nodiscard]] static QString summaryText(const core::codec::CalendarEvent &event);

Q_SIGNALS:
    void loaded();
    void fieldsChanged();
    void calendarsChanged();
    void busyChanged();
    void canSaveChanged();
    void conflictChanged();
    /// The event `luid` is on the phone and in the store as the form showed it.
    void saved(int luid);
    void errorOccurred(const QString &message);

private:
    void show(const core::codec::CalendarEvent &event);
    void changed();
    void setBusy(bool busy);
    void startSave(core::service::SaveMode mode);
    void reloadCalendars();
    [[nodiscard]] QDateTime startOf() const;
    [[nodiscard]] QDateTime endOf() const;
    /// The time reminders count back from: the start, for an all-day event local midnight.
    [[nodiscard]] QDateTime reminderBase(const core::codec::CalendarEvent &event) const;

    core::service::CalendarService *m_service = nullptr;
    std::weak_ptr<core::store::OrganizerStore> m_store;
    core::codec::CalendarEvent m_original;
    core::codec::CalendarEvent m_event;
    QList<core::codec::CalendarInfo> m_calendars;
    std::optional<core::service::EventSaveOutcome> m_conflict;
    QString m_startDate;
    QString m_startTime;
    QString m_endDate;
    QString m_endTime;
    /// The rule of a loaded event that repeatChoices() offers as its last entry.
    QString m_customRule;
    QList<qint64> m_reminderMinutes;
    int m_repeatIndex = 0;
    int m_colorIndex = 0;
    int m_calendarIndex = 0;
    bool m_connected = false;
    bool m_busy = false;
};

} // namespace qpm::ui
