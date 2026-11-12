#include "ui/organizer/TasksArea.h"

#include "core/codec/ICalendar.h"
#include "core/service/CalendarService.h"
#include "ui/organizer/OrganizerText.h"

#include <QFile>
#include <QSaveFile>
#include <QtConcurrentRun>

namespace qpm::ui {

namespace {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::codec::CalendarTask;
using core::codec::CategoryChange;

Result<QList<CalendarTask>> readTaskFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot open the file"));
    if (file.size() > core::codec::maxICalendarBytes) {
        return Error(ErrorCode::LimitExceeded,
                     QStringLiteral("File of %1 bytes is too large").arg(file.size()));
    }
    const Result<core::codec::ICalendarContent> content =
        core::codec::readICalendar(file.read(core::codec::maxICalendarBytes + 1));
    if (!content.hasValue())
        return content.error();
    return content.value().tasks;
}

Result<int> writeTaskFile(const QString &path, const QList<CalendarTask> &tasks)
{
    const QByteArray data = core::codec::writeICalendar({}, tasks);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return Error(ErrorCode::IoFailure, QStringLiteral("Cannot write the file"));
    return int(tasks.size());
}

} // namespace

TasksArea::TasksArea(core::service::CalendarService *service, QObject *parent)
    : QObject(parent), m_service(service), m_list(new TaskListModel(this)),
      m_editor(new TaskEditor(service, this)), m_categories(new CategoryModel(this))
{
    Q_ASSERT(service);
    connect(m_service, &core::service::CalendarService::calendarChanged, this, &TasksArea::reload);
    connect(m_editor, &TaskEditor::errorOccurred, this, &TasksArea::errorOccurred);
    connect(m_editor, &TaskEditor::saved, this, [this](int luid) { setCurrentLuid(luid); });
}

void TasksArea::setCurrentLuid(int luid)
{
    if (luid == m_currentLuid)
        return;
    m_currentLuid = luid;
    if (luid > 0 && m_editor->luid() != luid)
        static_cast<void>(m_editor->load(luid));
    Q_EMIT currentLuidChanged();
}

void TasksArea::setSearchText(const QString &text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    m_list->setSearchText(text);
    Q_EMIT searchTextChanged();
}

QStringList TasksArea::colorChoices() const
{
    return OrganizerText::colorChoices().mid(1);
}

void TasksArea::setStore(const std::shared_ptr<core::store::OrganizerStore> &store)
{
    m_store = store;
    m_editor->setStore(store);
    m_editor->setConnected(canEdit());
    m_currentLuid = 0;
    m_editor->newTask();
    reload();
    Q_EMIT currentLuidChanged();
    Q_EMIT hasPhoneChanged();
    Q_EMIT canEditChanged();
}

void TasksArea::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    m_editor->setConnected(canEdit());
    Q_EMIT canEditChanged();
}

void TasksArea::reload()
{
    if (!m_store) {
        m_list->setTasks({});
        m_categories->setCategories({});
        return;
    }
    const Result<QList<CalendarTask>> tasks = m_store->tasks();
    if (!tasks.hasValue())
        report(tasks.error());
    m_list->setTasks(tasks.hasValue() ? tasks.value() : QList<CalendarTask>());
    const Result<core::codec::CalMetadata> metadata = m_store->calendarMetadata();
    m_categories->setCategories(metadata.hasValue() ? metadata.value().taskCategories
                                                    : QList<core::codec::OrganizerCategory>());
    m_editor->setStore(m_store);
    if (m_currentLuid > 0 && !m_list->find(quint32(m_currentLuid))) {
        m_currentLuid = 0;
        m_editor->newTask();
        Q_EMIT currentLuidChanged();
    }
}

void TasksArea::report(const Error &error)
{
    const QString message = OrganizerText::errorText(error, OrganizerText::Area::Tasks);
    if (!message.isEmpty())
        Q_EMIT errorOccurred(message);
}

void TasksArea::beginWork()
{
    if (m_busy++ == 0)
        Q_EMIT busyChanged();
}

void TasksArea::endWork()
{
    if (m_busy > 0 && --m_busy == 0)
        Q_EMIT busyChanged();
}

void TasksArea::startNew()
{
    m_editor->newTask();
    if (m_currentLuid != 0) {
        m_currentLuid = 0;
        Q_EMIT currentLuidChanged();
    }
}

bool TasksArea::removeTask(int luid)
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
        if (m_currentLuid == luid)
            startNew();
    });
    return true;
}

bool TasksArea::setCompleted(int luid, bool completed)
{
    const CalendarTask *found = m_list->find(quint32(luid));
    if (!found)
        return false;
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    CalendarTask task = *found;
    task.completed = completed;
    beginWork();
    m_service->saveTask(task, core::service::SaveMode::Overwrite)
        .then(this, [this, luid](const Result<core::service::TaskSaveOutcome> &saved) {
            endWork();
            if (!saved.hasValue()) {
                report(saved.error());
                return;
            }
            if (m_currentLuid == luid)
                static_cast<void>(m_editor->load(luid));
        });
    return true;
}

void TasksArea::changeCategories(const QList<CategoryChange> &changes, const QString &done)
{
    beginWork();
    m_service->changeTaskCategories(changes).then(this, [this, done](const Result<void> &changed) {
        endWork();
        if (!changed.hasValue()) {
            report(changed.error());
            return;
        }
        Q_EMIT infoMessage(done);
    });
}

bool TasksArea::addCategory(const QString &name, int colorIndex)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        Q_EMIT errorOccurred(tr("Enter a name for the category."));
        return false;
    }
    if (m_categories->names().contains(trimmed, Qt::CaseInsensitive)) {
        Q_EMIT errorOccurred(tr("There is a category of this name already."));
        return false;
    }
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    const quint32 color = OrganizerText::colorValue(colorIndex + 1).value_or(0);
    changeCategories({{CategoryChange::Kind::Add, {0, trimmed, color}}},
                     tr("The category %1 is added.").arg(trimmed));
    return true;
}

bool TasksArea::editCategory(int row, const QString &name, int colorIndex)
{
    core::codec::OrganizerCategory category = m_categories->at(row);
    const QString trimmed = name.trimmed();
    if (category.id <= 0 || trimmed.isEmpty())
        return false;
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    category.name = trimmed;
    category.color = OrganizerText::colorValue(colorIndex + 1).value_or(category.color);
    changeCategories({{CategoryChange::Kind::Edit, category}},
                     tr("The category %1 is changed.").arg(trimmed));
    return true;
}

bool TasksArea::removeCategory(int row)
{
    const core::codec::OrganizerCategory category = m_categories->at(row);
    if (category.id <= 0)
        return false;
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    changeCategories({{CategoryChange::Kind::Delete, category}},
                     tr("The category %1 is deleted.").arg(category.name));
    return true;
}

bool TasksArea::exportTo(const QUrl &url)
{
    if (!m_store) {
        report(Error(ErrorCode::InvalidInput, QStringLiteral("No phone chosen")));
        return false;
    }
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    const Result<QList<CalendarTask>> tasks = m_store->tasks();
    if (!tasks.hasValue()) {
        report(tasks.error());
        return false;
    }
    const QString path = url.toLocalFile();
    const QList<CalendarTask> list = tasks.value();
    beginWork();
    QtConcurrent::run([path, list]() {
        return writeTaskFile(path, list);
    }).then(this, [this](const Result<int> &written) {
        endWork();
        if (!written.hasValue()) {
            Q_EMIT errorOccurred(tr("The file cannot be written. Choose another folder."));
            return;
        }
        Q_EMIT infoMessage(tr("%n task(s) exported.", nullptr, written.value()));
    });
    return true;
}

bool TasksArea::importFrom(const QUrl &url)
{
    if (!canEdit()) {
        report(Error(ErrorCode::ConnectionClosed, QStringLiteral("Not connected")));
        return false;
    }
    if (!url.isLocalFile()) {
        Q_EMIT errorOccurred(tr("Choose a file on this computer."));
        return false;
    }
    const QString path = url.toLocalFile();
    beginWork();
    QtConcurrent::run([path]() {
        return readTaskFile(path);
    }).then(this, [this](const Result<QList<CalendarTask>> &read) {
        if (!read.hasValue() || read.value().isEmpty()) {
            endWork();
            Q_EMIT errorOccurred(!read.hasValue() && read.error().code() == ErrorCode::IoFailure
                                     ? tr("The file cannot be opened. Check that it exists.")
                                     : tr("The file holds no tasks. Choose an iCalendar file "
                                          "with tasks."));
            return;
        }
        m_service->importItems({}, read.value(), core::codec::CalendarInfo())
            .then(this, [this](const Result<core::service::OrganizerImportOutcome> &imported) {
                endWork();
                if (!imported.hasValue()) {
                    report(imported.error());
                    return;
                }
                QString message = tr("%n task(s) imported.", nullptr, imported.value().created);
                if (imported.value().skipped > 0) {
                    message += u' ' + tr("%n task(s) were left out, because they lack a title.",
                                         nullptr, imported.value().skipped);
                }
                Q_EMIT infoMessage(message);
            });
    });
    return true;
}

} // namespace qpm::ui
