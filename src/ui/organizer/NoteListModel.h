#pragma once

#include "core/codec/VNote.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::ui {

/**
 * The notes of the current phone, newest change first, narrowed by the search
 * of the area and by a category. Filled by NotesArea.
 */
class NoteListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NoteListModel)
    QML_UNCREATABLE("Provided by NotesArea")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Notes stored for the phone, before any filter.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged FINAL)
    /// Category name to show only its notes; empty shows all.
    Q_PROPERTY(QString categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY
                   categoryFilterChanged FINAL)

public:
    enum Role {
        LuidRole = Qt::UserRole + 1,
        TitleRole,
        PreviewRole,
        ModifiedTextRole,
        CategoriesTextRole,
    };

    explicit NoteListModel(QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return rowCount(); }
    [[nodiscard]] int totalCount() const { return int(m_all.size()); }
    [[nodiscard]] QString categoryFilter() const { return m_category; }
    void setCategoryFilter(const QString &category);

    /// Replaces every note; the filters stay.
    void setNotes(const QList<core::codec::Note> &notes);
    /// Shows only notes whose text or category holds `text`, without regard to case.
    void setSearchText(const QString &text);
    /// The stored note `luid`, also when it is filtered out; nothing when unknown.
    [[nodiscard]] const core::codec::Note *find(quint32 luid) const;

    /// The first line of a note, shortened, as its title.
    [[nodiscard]] static QString titleOf(const QString &body);

    /// The row of the note `luid`, or -1 when it is not shown.
    [[nodiscard]] Q_INVOKABLE int rowOf(int luid) const;
    /// The LUID in `row`, or 0.
    [[nodiscard]] Q_INVOKABLE int luidAt(int row) const;
    /// The title of the note `luid`, also when it is filtered out; empty when unknown.
    [[nodiscard]] Q_INVOKABLE QString titleOfNote(int luid) const;

Q_SIGNALS:
    void countChanged();
    void categoryFilterChanged();

private:
    void refilter();

    QList<core::codec::Note> m_all;
    QList<qsizetype> m_rows;
    QString m_search;
    QString m_category;
};

} // namespace qpm::ui
