#pragma once

#include "core/Error.h"
#include "core/store/OrganizerStore.h"
#include "ui/organizer/CategoryModel.h"
#include "ui/organizer/TaskEditor.h"
#include "ui/organizer/TaskListModel.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace qpm::core::service {
class CalendarService;
}

namespace qpm::ui {

/**
 * The tasks area of the window: the list with its filter, the form of the
 * task chosen, ticking tasks off, the task categories with their colours,
 * and iCalendar import and export of the tasks. Every failure is reported
 * once through errorOccurred() as a translated text that says what to do.
 *
 * Owned by AppController. The tasks live in the calendar of the phone, so
 * the area shares the calendar service and the store with CalendarArea and
 * reloads whenever that service reports a change. The tasks are shown
 * without a connection; changing them needs one. No call blocks.
 */
class TasksArea : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TasksArea)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(qpm::ui::TaskListModel *list READ list CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::TaskEditor *editor READ editor CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::CategoryModel *categories READ categories CONSTANT FINAL)
    /// The task in the form; 0 for none or a new one.
    Q_PROPERTY(
        int currentLuid READ currentLuid WRITE setCurrentLuid NOTIFY currentLuidChanged FINAL)
    Q_PROPERTY(
        QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged FINAL)
    Q_PROPERTY(bool hasPhone READ hasPhone NOTIFY hasPhoneChanged FINAL)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY canEditChanged FINAL)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// The colours a category can take.
    Q_PROPERTY(QStringList colorChoices READ colorChoices CONSTANT FINAL)

public:
    /// `service` belongs to CalendarArea and must outlive this object.
    explicit TasksArea(core::service::CalendarService *service, QObject *parent = nullptr);

    [[nodiscard]] TaskListModel *list() const { return m_list; }
    [[nodiscard]] TaskEditor *editor() const { return m_editor; }
    [[nodiscard]] CategoryModel *categories() const { return m_categories; }
    [[nodiscard]] int currentLuid() const { return m_currentLuid; }
    void setCurrentLuid(int luid);
    [[nodiscard]] QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    [[nodiscard]] bool hasPhone() const { return m_store != nullptr; }
    [[nodiscard]] bool canEdit() const { return m_connected && m_store; }
    [[nodiscard]] bool isBusy() const { return m_busy > 0; }
    [[nodiscard]] QStringList colorChoices() const;

    /// Shows the tasks of `store`, which CalendarArea shares; null shows none.
    void setStore(const std::shared_ptr<core::store::OrganizerStore> &store);
    void setConnected(bool connected);

    /// Empties the form for a new task.
    Q_INVOKABLE void startNew();
    Q_INVOKABLE bool removeTask(int luid);
    /// Ticks the task `luid` off or opens it again.
    Q_INVOKABLE bool setCompleted(int luid, bool completed);
    /// Adds a task category with the colour at `colorIndex` of colorChoices.
    Q_INVOKABLE bool addCategory(const QString &name, int colorIndex);
    /// Renames and recolours the category in `row` of categories.
    Q_INVOKABLE bool editCategory(int row, const QString &name, int colorIndex);
    /// Deletes the category in `row`; tasks keep their other categories.
    Q_INVOKABLE bool removeCategory(int row);
    /// Writes every task to the iCalendar file `url`.
    Q_INVOKABLE bool exportTo(const QUrl &url);
    /// Reads the tasks of the iCalendar file `url` and creates them on the phone.
    Q_INVOKABLE bool importFrom(const QUrl &url);

Q_SIGNALS:
    void currentLuidChanged();
    void searchTextChanged();
    void hasPhoneChanged();
    void canEditChanged();
    void busyChanged();
    void errorOccurred(const QString &message);
    void infoMessage(const QString &message);

private:
    void reload();
    void report(const core::Error &error);
    void beginWork();
    void endWork();
    void changeCategories(const QList<core::codec::CategoryChange> &changes, const QString &done);

    core::service::CalendarService *m_service = nullptr;
    std::shared_ptr<core::store::OrganizerStore> m_store;
    TaskListModel *m_list = nullptr;
    TaskEditor *m_editor = nullptr;
    CategoryModel *m_categories = nullptr;
    QString m_searchText;
    int m_currentLuid = 0;
    int m_busy = 0;
    bool m_connected = false;
};

} // namespace qpm::ui
