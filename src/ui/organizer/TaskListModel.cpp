#include "ui/organizer/TaskListModel.h"

#include "ui/organizer/OrganizerText.h"

namespace qpm::ui {

namespace {

bool isOverdue(const core::codec::CalendarTask &task)
{
    return !task.completed && task.due.isValid() && task.due < QDate::currentDate();
}

} // namespace

TaskListModel::TaskListModel(QObject *parent) : QAbstractListModel(parent) {}

int TaskListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant TaskListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const core::codec::CalendarTask &task = m_all.at(m_rows.at(index.row()));
    switch (role) {
    case LuidRole:
        return int(task.luid);
    case Qt::DisplayRole:
    case SummaryRole:
        return task.summary;
    case DescriptionRole:
        return task.description;
    case DueTextRole:
        if (!task.due.isValid())
            return QString();
        return isOverdue(task) ? tr("Overdue since %1").arg(OrganizerText::formatDate(task.due))
                               : tr("Due %1").arg(OrganizerText::formatDate(task.due));
    case OverdueRole:
        return isOverdue(task);
    case CompletedRole:
        return task.completed;
    case PriorityRole:
        return static_cast<int>(task.priority);
    case PriorityTextRole:
        return OrganizerText::priorityChoices().value(static_cast<int>(task.priority));
    case CategoriesTextRole:
        return task.categories.join(QLatin1StringView(", "));
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> TaskListModel::roleNames() const
{
    return {
        {LuidRole, QByteArrayLiteral("luid")},
        {SummaryRole, QByteArrayLiteral("summary")},
        {DescriptionRole, QByteArrayLiteral("description")},
        {DueTextRole, QByteArrayLiteral("dueText")},
        {OverdueRole, QByteArrayLiteral("overdue")},
        {CompletedRole, QByteArrayLiteral("completed")},
        {PriorityRole, QByteArrayLiteral("priority")},
        {PriorityTextRole, QByteArrayLiteral("priorityText")},
        {CategoriesTextRole, QByteArrayLiteral("categoriesText")},
    };
}

void TaskListModel::setFilter(Filter filter)
{
    if (filter == m_filter)
        return;
    m_filter = filter;
    Q_EMIT filterChanged();
    refilter();
}

void TaskListModel::setFilterIndex(int index)
{
    if (index >= static_cast<int>(Filter::Open) && index <= static_cast<int>(Filter::All))
        setFilter(static_cast<Filter>(index));
}

void TaskListModel::setTasks(const QList<core::codec::CalendarTask> &tasks)
{
    const int total = totalCount();
    m_all = tasks;
    refilter();
    if (total != totalCount())
        Q_EMIT countChanged();
}

void TaskListModel::setSearchText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_search)
        return;
    m_search = trimmed;
    refilter();
}

const core::codec::CalendarTask *TaskListModel::find(quint32 luid) const
{
    for (const core::codec::CalendarTask &task : m_all) {
        if (task.luid == luid)
            return &task;
    }
    return nullptr;
}

int TaskListModel::rowOf(int luid) const
{
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (int(m_all.at(m_rows.at(row)).luid) == luid)
            return int(row);
    }
    return -1;
}

int TaskListModel::luidAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? int(m_all.at(m_rows.at(row)).luid) : 0;
}

bool TaskListModel::isCompleted(int row) const
{
    return row >= 0 && row < m_rows.size() && m_all.at(m_rows.at(row)).completed;
}

QString TaskListModel::titleOf(int luid) const
{
    const core::codec::CalendarTask *task = find(quint32(luid));
    return task ? task->summary : QString();
}

void TaskListModel::refilter()
{
    const int before = int(m_rows.size());
    beginResetModel();
    m_rows.clear();
    for (qsizetype i = 0; i < m_all.size(); ++i) {
        const core::codec::CalendarTask &task = m_all.at(i);
        if ((m_filter == Filter::Open && task.completed) || (m_filter == Filter::Done && !task.completed))
            continue;
        if (!m_search.isEmpty() && !task.summary.contains(m_search, Qt::CaseInsensitive) &&
            !task.description.contains(m_search, Qt::CaseInsensitive) &&
            !task.categories.join(u'\n').contains(m_search, Qt::CaseInsensitive)) {
            continue;
        }
        m_rows.append(i);
    }
    endResetModel();
    if (before != m_rows.size())
        Q_EMIT countChanged();
}

} // namespace qpm::ui
