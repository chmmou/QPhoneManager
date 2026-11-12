#pragma once

#include "core/codec/PhoneVCal.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * The tasks of the current phone, open ones first and then by due date,
 * narrowed by the search of the area and by their state. Filled by TasksArea.
 */
class TaskListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TaskListModel)
    QML_UNCREATABLE("Provided by TasksArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Tasks stored for the phone, before any filter.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged FINAL)
    Q_PROPERTY(Filter filter READ filter WRITE setFilter NOTIFY filterChanged FINAL)
    /// filter as the position of its tab: open, done, all.
    Q_PROPERTY(int filterIndex READ filterIndex WRITE setFilterIndex NOTIFY filterChanged FINAL)

public:
    enum class Filter { Open, Done, All };
    Q_ENUM(Filter)

    enum Role {
        LuidRole = Qt::UserRole + 1,
        SummaryRole,
        DescriptionRole,
        DueTextRole,
        OverdueRole,
        CompletedRole,
        PriorityRole,
        PriorityTextRole,
        CategoriesTextRole,
    };

    explicit TaskListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }
    [[nodiscard]] int totalCount() const { return int(m_all.size()); }
    [[nodiscard]] Filter filter() const { return m_filter; }
    void setFilter(Filter filter);
    [[nodiscard]] int filterIndex() const { return static_cast<int>(m_filter); }
    void setFilterIndex(int index);

    /// Replaces every task; the filters stay.
    void setTasks(const QList<core::codec::CalendarTask> &tasks);
    /// Shows only tasks whose title, text or category holds `text`, without regard to case.
    void setSearchText(const QString &text);
    /// The stored task `luid`, also when it is filtered out; nothing when unknown.
    [[nodiscard]] const core::codec::CalendarTask *find(quint32 luid) const;

    /// The row of the task `luid`, or -1 when it is not shown.
    [[nodiscard]] Q_INVOKABLE int rowOf(int luid) const;
    /// The LUID in `row`, or 0.
    [[nodiscard]] Q_INVOKABLE int luidAt(int row) const;
    /// Whether the task in `row` is done.
    [[nodiscard]] Q_INVOKABLE bool isCompleted(int row) const;
    /// The title of the task `luid`, also when it is filtered out; empty when unknown.
    [[nodiscard]] Q_INVOKABLE QString titleOf(int luid) const;

Q_SIGNALS:
    void countChanged();
    void filterChanged();

private:
    void refilter();

    QList<core::codec::CalendarTask> m_all;
    QList<qsizetype> m_rows;
    QString m_search;
    Filter m_filter = Filter::Open;
};

} // namespace qpm::ui
