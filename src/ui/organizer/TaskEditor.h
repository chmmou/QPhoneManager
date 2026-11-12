#pragma once

#include "core/codec/CalMetadata.h"
#include "core/codec/PhoneVCal.h"
#include "core/service/CalendarService.h"

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
 * The form for one task, new or loaded from the store. Dates and the time of
 * the reminder are typed as text in the form of the locale; problem says what
 * keeps them from being used. A category typed here that the phone does not
 * know yet is created there with the task.
 *
 * save() sends the whole task. When the task changed on the phone since it
 * was loaded, the save stops, hasConflict turns true and both versions are
 * shown; keepOwnVersion() or takePhoneVersion() decides.
 *
 * Owned by TasksArea, which hands in the store and the connection state.
 * Saving needs a connection; everything else works without one.
 */
class TaskEditor : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TaskEditor)
    QML_UNCREATABLE("Provided by TasksArea")

    Q_PROPERTY(bool isNew READ isNew NOTIFY loaded FINAL)
    Q_PROPERTY(int luid READ luid NOTIFY loaded FINAL)
    Q_PROPERTY(QString summary READ summary WRITE setSummary NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(
        QString description READ description WRITE setDescription NOTIFY fieldsChanged FINAL)
    /// May stay empty.
    Q_PROPERTY(QString startDate READ startDate WRITE setStartDate NOTIFY fieldsChanged FINAL)
    /// May stay empty.
    Q_PROPERTY(QString dueDate READ dueDate WRITE setDueDate NOTIFY fieldsChanged FINAL)
    /// Both empty for no reminder.
    Q_PROPERTY(
        QString reminderDate READ reminderDate WRITE setReminderDate NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(
        QString reminderTime READ reminderTime WRITE setReminderTime NOTIFY fieldsChanged FINAL)
    /// The repetitions offered; a rule of the phone the form cannot build comes last.
    Q_PROPERTY(QStringList repeatChoices READ repeatChoices NOTIFY loaded FINAL)
    Q_PROPERTY(int repeatIndex READ repeatIndex WRITE setRepeatIndex NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QStringList priorityChoices READ priorityChoices CONSTANT FINAL)
    Q_PROPERTY(
        int priorityIndex READ priorityIndex WRITE setPriorityIndex NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(bool completed READ completed WRITE setCompleted NOTIFY fieldsChanged FINAL)
    /// The categories of the phone and those the task names, by name.
    Q_PROPERTY(QStringList categoryNames READ categoryNames NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QStringList selectedCategories READ selectedCategories NOTIFY fieldsChanged FINAL)
    /// Why the form cannot be saved as it is; empty when it can.
    Q_PROPERTY(QString problem READ problem NOTIFY fieldsChanged FINAL)
    /// What is wrong with the order of start and due date; empty when nothing is.
    Q_PROPERTY(QString dateOrderProblem READ dateOrderProblem NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString dateExample READ dateExample CONSTANT FINAL)
    Q_PROPERTY(QString timeExample READ timeExample CONSTANT FINAL)
    /// The form of a date with letters, for an empty date field.
    Q_PROPERTY(QString dateHint READ dateHint CONSTANT FINAL)
    /// The form of a time with letters, for an empty time field.
    Q_PROPERTY(QString timeHint READ timeHint CONSTANT FINAL)
    Q_PROPERTY(bool modified READ isModified NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// Connected, not busy, no problem.
    Q_PROPERTY(bool canSave READ canSave NOTIFY canSaveChanged FINAL)
    Q_PROPERTY(bool hasConflict READ hasConflict NOTIFY conflictChanged FINAL)
    Q_PROPERTY(QString ownVersionText READ ownVersionText NOTIFY conflictChanged FINAL)
    Q_PROPERTY(QString phoneVersionText READ phoneVersionText NOTIFY conflictChanged FINAL)

public:
    /// `service` must outlive this object.
    explicit TaskEditor(core::service::CalendarService *service, QObject *parent = nullptr);

    void setStore(const std::shared_ptr<core::store::OrganizerStore> &store);
    void setConnected(bool connected);

    [[nodiscard]] bool isNew() const { return m_original.luid == 0; }
    [[nodiscard]] int luid() const { return int(m_original.luid); }
    [[nodiscard]] QString summary() const { return m_task.summary; }
    void setSummary(const QString &value);
    [[nodiscard]] QString description() const { return m_task.description; }
    void setDescription(const QString &value);
    [[nodiscard]] QString startDate() const { return m_startDate; }
    void setStartDate(const QString &value);
    [[nodiscard]] QString dueDate() const { return m_dueDate; }
    void setDueDate(const QString &value);
    [[nodiscard]] QString reminderDate() const { return m_reminderDate; }
    void setReminderDate(const QString &value);
    [[nodiscard]] QString reminderTime() const { return m_reminderTime; }
    void setReminderTime(const QString &value);
    [[nodiscard]] QStringList repeatChoices() const;
    [[nodiscard]] int repeatIndex() const { return m_repeatIndex; }
    void setRepeatIndex(int index);
    [[nodiscard]] QStringList priorityChoices() const;
    [[nodiscard]] int priorityIndex() const { return static_cast<int>(m_task.priority); }
    void setPriorityIndex(int index);
    [[nodiscard]] bool completed() const { return m_task.completed; }
    void setCompleted(bool completed);
    [[nodiscard]] QStringList categoryNames() const;
    [[nodiscard]] QStringList selectedCategories() const { return m_task.categories; }
    [[nodiscard]] QString problem() const;
    [[nodiscard]] QString dateOrderProblem() const;
    [[nodiscard]] QString dateExample() const;
    [[nodiscard]] QString timeExample() const;
    [[nodiscard]] QString dateHint() const;
    [[nodiscard]] QString timeHint() const;
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] bool isBusy() const { return m_busy; }
    [[nodiscard]] bool canSave() const;
    [[nodiscard]] bool hasConflict() const { return m_conflict.has_value(); }
    [[nodiscard]] QString ownVersionText() const;
    [[nodiscard]] QString phoneVersionText() const;

    /// Empties the form for a new task.
    Q_INVOKABLE void newTask();
    /// Fills the form with the stored task `luid`; false and an error when it is unknown.
    Q_INVOKABLE bool load(int luid);
    /// Starts saving; false and an error when the form cannot be saved now.
    Q_INVOKABLE bool save();
    /// After a conflict: writes this form over the phone's version.
    Q_INVOKABLE bool keepOwnVersion();
    /// After a conflict: drops this form and keeps the phone's version.
    Q_INVOKABLE bool takePhoneVersion();
    Q_INVOKABLE void setCategorySelected(const QString &name, bool selected);
    /// Adds a category to this task; the phone creates it on saving. False for an empty name.
    Q_INVOKABLE bool addCategory(const QString &name);

    /// The task as the form holds it.
    [[nodiscard]] core::codec::CalendarTask edited() const;

    /// A few lines with the fields of `task`, for comparing two versions.
    [[nodiscard]] static QString summaryText(const core::codec::CalendarTask &task);

Q_SIGNALS:
    void loaded();
    void fieldsChanged();
    void busyChanged();
    void canSaveChanged();
    void conflictChanged();
    /// The task `luid` is on the phone and in the store as the form showed it.
    void saved(int luid);
    void errorOccurred(const QString &message);

private:
    void show(const core::codec::CalendarTask &task);
    void changed();
    void setBusy(bool busy);
    void startSave(core::service::SaveMode mode);
    void reloadCategories();

    core::service::CalendarService *m_service = nullptr;
    std::weak_ptr<core::store::OrganizerStore> m_store;
    core::codec::CalendarTask m_original;
    core::codec::CalendarTask m_task;
    QStringList m_knownCategories;
    std::optional<core::service::TaskSaveOutcome> m_conflict;
    QString m_startDate;
    QString m_dueDate;
    QString m_reminderDate;
    QString m_reminderTime;
    QString m_customRule;
    int m_repeatIndex = 0;
    bool m_connected = false;
    bool m_busy = false;
};

} // namespace qpm::ui
