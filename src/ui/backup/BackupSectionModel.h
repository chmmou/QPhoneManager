#pragma once

#include "core/backup/BackupArchive.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * The six sections of a backup as a list to choose from: contacts, calendar,
 * tasks, notes, call log and messages, always in this order. Each row says
 * whether it is chosen, whether it can be chosen, and a line of detail that
 * BackupController fills in. Lives in the main thread.
 */
class BackupSectionModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BackupSectionModel)
    QML_UNCREATABLE("Provided by BackupController")

    /// The keys of the chosen rows that can be chosen, see core::backup::sectionKey().
    Q_PROPERTY(QStringList chosenKeys READ chosenKeys NOTIFY chosenChanged FINAL)

public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        ChosenRole,
        SelectableRole,
        DetailRole,
    };
    Q_ENUM(Role)

    explicit BackupSectionModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QStringList chosenKeys() const;

    /// Chooses the row or takes it out; a row that cannot be chosen stays out.
    Q_INVOKABLE void setChosen(int row, bool chosen);

    [[nodiscard]] core::backup::BackupSection sectionAt(int row) const;
    [[nodiscard]] int rowOf(core::backup::BackupSection section) const;
    /// Sets what can be chosen and what is chosen; an unselectable row is never chosen.
    void setState(core::backup::BackupSection section, bool selectable, bool chosen);
    void setDetail(core::backup::BackupSection section, const QString &detail);
    /// Empties every detail line.
    void clearDetails();

Q_SIGNALS:
    void chosenChanged();

private:
    struct Row
    {
        core::backup::BackupSection section = core::backup::BackupSection::Contacts;
        bool chosen = true;
        bool selectable = true;
        QString detail;
    };

    [[nodiscard]] static QString titleOf(core::backup::BackupSection section);

    QList<Row> m_rows;
};

} // namespace qpm::ui
