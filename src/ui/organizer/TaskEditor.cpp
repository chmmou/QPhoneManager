#include "ui/organizer/TaskEditor.h"

#include "core/store/OrganizerStore.h"
#include "ui/organizer/OrganizerText.h"

namespace qpm::ui {

namespace {

using core::codec::CalendarTask;

constexpr int standardRepeatChoices = 5;

} // namespace

TaskEditor::TaskEditor(core::service::CalendarService *service, QObject *parent)
    : QObject(parent), m_service(service)
{
    Q_ASSERT(service);
    newTask();
}

void TaskEditor::setStore(const std::shared_ptr<core::store::OrganizerStore> &store)
{
    m_store = store;
    reloadCategories();
    Q_EMIT canSaveChanged();
}

void TaskEditor::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    Q_EMIT canSaveChanged();
}

void TaskEditor::changed()
{
    Q_EMIT fieldsChanged();
    Q_EMIT canSaveChanged();
}

void TaskEditor::setSummary(const QString &value)
{
    if (value == m_task.summary)
        return;
    m_task.summary = value;
    changed();
}

void TaskEditor::setDescription(const QString &value)
{
    if (value == m_task.description)
        return;
    m_task.description = value;
    changed();
}

void TaskEditor::setStartDate(const QString &value)
{
    if (value == m_startDate)
        return;
    m_startDate = value;
    changed();
}

void TaskEditor::setDueDate(const QString &value)
{
    if (value == m_dueDate)
        return;
    m_dueDate = value;
    changed();
}

void TaskEditor::setReminderDate(const QString &value)
{
    if (value == m_reminderDate)
        return;
    m_reminderDate = value;
    changed();
}

void TaskEditor::setReminderTime(const QString &value)
{
    if (value == m_reminderTime)
        return;
    m_reminderTime = value;
    changed();
}

QStringList TaskEditor::repeatChoices() const
{
    QStringList choices = OrganizerText::repeatChoices();
    if (!m_customRule.isEmpty())
        choices.append(OrganizerText::repeatText(m_customRule));
    return choices;
}

void TaskEditor::setRepeatIndex(int index)
{
    if (index == m_repeatIndex || index < 0 || index >= repeatChoices().size())
        return;
    m_repeatIndex = index;
    changed();
}

QStringList TaskEditor::priorityChoices() const
{
    return OrganizerText::priorityChoices();
}

void TaskEditor::setPriorityIndex(int index)
{
    if (index < 0 || index >= priorityChoices().size() || index == priorityIndex())
        return;
    m_task.priority = static_cast<core::codec::TaskPriority>(index);
    changed();
}

void TaskEditor::setCompleted(bool completed)
{
    if (completed == m_task.completed)
        return;
    m_task.completed = completed;
    changed();
}

QStringList TaskEditor::categoryNames() const
{
    QStringList names = m_knownCategories;
    for (const QString &name : m_task.categories) {
        if (!names.contains(name, Qt::CaseInsensitive))
            names.append(name);
    }
    return names;
}

QString TaskEditor::dateExample() const
{
    return OrganizerText::dateExample();
}

QString TaskEditor::timeExample() const
{
    return OrganizerText::timeExample();
}

QString TaskEditor::dateHint() const
{
    return OrganizerText::dateHint();
}

QString TaskEditor::timeHint() const
{
    return OrganizerText::timeHint();
}

QString TaskEditor::problem() const
{
    if (m_task.summary.trimmed().isEmpty())
        return tr("Enter a title.");
    if (!m_startDate.trimmed().isEmpty() && !OrganizerText::parseDate(m_startDate).isValid())
        return tr("Enter the start date like %1, or leave it empty.").arg(dateExample());
    if (!m_dueDate.trimmed().isEmpty() && !OrganizerText::parseDate(m_dueDate).isValid())
        return tr("Enter the due date like %1, or leave it empty.").arg(dateExample());
    if (const QString order = dateOrderProblem(); !order.isEmpty())
        return order;
    const bool reminderDate = !m_reminderDate.trimmed().isEmpty();
    const bool reminderTime = !m_reminderTime.trimmed().isEmpty();
    if (reminderDate != reminderTime)
        return tr("Enter both the date and the time of the reminder, or neither.");
    if (reminderDate && !OrganizerText::parseDate(m_reminderDate).isValid())
        return tr("Enter the date of the reminder like %1.").arg(dateExample());
    if (reminderTime && !OrganizerText::parseTime(m_reminderTime).isValid())
        return tr("Enter the time of the reminder like %1.").arg(timeExample());
    return {};
}

QString TaskEditor::dateOrderProblem() const
{
    const QDate start = OrganizerText::parseDate(m_startDate);
    const QDate due = OrganizerText::parseDate(m_dueDate);
    if (start.isValid() && due.isValid() && start > due)
        return tr("The start lies after the due date. Change one of the two dates.");
    return {};
}

CalendarTask TaskEditor::edited() const
{
    CalendarTask task = m_task;
    task.start = OrganizerText::parseDate(m_startDate);
    task.due = OrganizerText::parseDate(m_dueDate);
    const QDate reminderDay = OrganizerText::parseDate(m_reminderDate);
    const QTime reminderTime = OrganizerText::parseTime(m_reminderTime);
    task.reminder = reminderDay.isValid() && reminderTime.isValid()
                        ? QDateTime(reminderDay, reminderTime).toUTC()
                        : QDateTime();
    task.rrule = m_repeatIndex >= standardRepeatChoices ? m_customRule
                                                        : OrganizerText::repeatRule(m_repeatIndex);
    return task;
}

bool TaskEditor::isModified() const
{
    return isNew() || !core::codec::sameTaskContent(edited(), m_original);
}

bool TaskEditor::canSave() const
{
    return m_connected && !m_busy && !m_store.expired() && problem().isEmpty();
}

QString TaskEditor::summaryText(const CalendarTask &task)
{
    QStringList lines;
    lines.append(task.summary);
    if (task.completed)
        lines.append(tr("Done"));
    if (task.start.isValid())
        lines.append(tr("Start: %1").arg(OrganizerText::formatDate(task.start)));
    if (task.due.isValid())
        lines.append(tr("Due: %1").arg(OrganizerText::formatDate(task.due)));
    if (task.reminder.isValid()) {
        const QDateTime local = task.reminder.toLocalTime();
        lines.append(tr("Reminder: %1 %2").arg(OrganizerText::formatDate(local.date()),
                                               OrganizerText::formatTime(local.time())));
    }
    lines.append(tr("Priority: %1")
                     .arg(OrganizerText::priorityChoices().value(static_cast<int>(task.priority))));
    if (!task.categories.isEmpty())
        lines.append(tr("Categories: %1").arg(task.categories.join(QLatin1StringView(", "))));
    const QString repeat = OrganizerText::repeatText(task.rrule);
    if (!repeat.isEmpty())
        lines.append(repeat);
    if (!task.description.trimmed().isEmpty())
        lines.append(task.description.trimmed());
    return lines.join(u'\n');
}

QString TaskEditor::ownVersionText() const
{
    return m_conflict ? summaryText(edited()) : QString();
}

QString TaskEditor::phoneVersionText() const
{
    return m_conflict ? summaryText(m_conflict->phoneVersion) : QString();
}

void TaskEditor::reloadCategories()
{
    m_knownCategories.clear();
    if (const std::shared_ptr<core::store::OrganizerStore> store = m_store.lock()) {
        const core::Result<core::codec::CalMetadata> metadata = store->calendarMetadata();
        if (metadata.hasValue()) {
            for (const core::codec::OrganizerCategory &category : metadata.value().taskCategories)
                m_knownCategories.append(category.name);
        }
    }
    Q_EMIT fieldsChanged();
}

void TaskEditor::show(const CalendarTask &task)
{
    m_original = task;
    m_task = task;
    m_startDate = OrganizerText::formatDate(task.start);
    m_dueDate = OrganizerText::formatDate(task.due);
    const QDateTime reminder = task.reminder.toLocalTime();
    m_reminderDate = task.reminder.isValid() ? OrganizerText::formatDate(reminder.date()) : QString();
    m_reminderTime = task.reminder.isValid() ? OrganizerText::formatTime(reminder.time()) : QString();
    const std::optional<int> repeat = OrganizerText::repeatIndex(task.rrule);
    m_customRule = repeat ? QString() : task.rrule;
    m_repeatIndex = repeat ? *repeat : standardRepeatChoices;
    const bool hadConflict = m_conflict.has_value();
    m_conflict.reset();
    Q_EMIT loaded();
    changed();
    if (hadConflict)
        Q_EMIT conflictChanged();
}

void TaskEditor::newTask()
{
    reloadCategories();
    show(CalendarTask());
}

bool TaskEditor::load(int luid)
{
    const std::shared_ptr<core::store::OrganizerStore> store = m_store.lock();
    const core::Result<CalendarTask> task =
        store && luid > 0 ? store->task(quint32(luid))
                          : core::Result<CalendarTask>(
                                core::Error(core::ErrorCode::NotFound, QStringLiteral("No task")));
    if (!task.hasValue()) {
        Q_EMIT errorOccurred(OrganizerText::errorText(task.error(), OrganizerText::Area::Tasks));
        return false;
    }
    reloadCategories();
    show(task.value());
    return true;
}

void TaskEditor::setCategorySelected(const QString &name, bool selected)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || m_task.categories.contains(trimmed, Qt::CaseInsensitive) == selected)
        return;
    if (selected) {
        m_task.categories.append(trimmed);
    } else {
        m_task.categories.removeIf([&trimmed](const QString &category) {
            return category.compare(trimmed, Qt::CaseInsensitive) == 0;
        });
    }
    changed();
}

bool TaskEditor::addCategory(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;
    setCategorySelected(trimmed, true);
    return true;
}

void TaskEditor::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
    Q_EMIT canSaveChanged();
}

bool TaskEditor::save()
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
            OrganizerText::Area::Tasks));
        return false;
    }
    startSave(core::service::SaveMode::CheckConflict);
    return true;
}

void TaskEditor::startSave(core::service::SaveMode mode)
{
    setBusy(true);
    m_service->saveTask(edited(), mode)
        .then(this, [this](const core::Result<core::service::TaskSaveOutcome> &outcome) {
            setBusy(false);
            if (!outcome.hasValue()) {
                const QString message =
                    OrganizerText::errorText(outcome.error(), OrganizerText::Area::Tasks);
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

bool TaskEditor::keepOwnVersion()
{
    if (!m_conflict || m_busy)
        return false;
    m_conflict.reset();
    Q_EMIT conflictChanged();
    startSave(core::service::SaveMode::Overwrite);
    return true;
}

bool TaskEditor::takePhoneVersion()
{
    if (!m_conflict || m_busy)
        return false;
    const core::Result<void> kept = m_service->keepPhoneVersion(m_conflict->phoneObject);
    if (!kept.hasValue()) {
        Q_EMIT errorOccurred(OrganizerText::errorText(kept.error(), OrganizerText::Area::Tasks));
        return false;
    }
    const int luid = int(m_original.luid);
    static_cast<void>(load(luid));
    Q_EMIT saved(luid);
    return true;
}

} // namespace qpm::ui
