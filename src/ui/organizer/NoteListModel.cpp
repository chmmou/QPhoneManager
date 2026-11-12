#include "ui/organizer/NoteListModel.h"

#include "ui/organizer/OrganizerText.h"

#include <QLocale>

namespace qpm::ui {

namespace {

constexpr qsizetype titleLength = 60;
constexpr qsizetype previewLength = 120;

QString shortened(const QString &text, qsizetype length)
{
    const QString simple = text.simplified();
    return simple.size() <= length ? simple : simple.left(length - 3).trimmed() + QStringLiteral("...");
}

} // namespace

NoteListModel::NoteListModel(QObject *parent) : QAbstractListModel(parent) {}

int NoteListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant NoteListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const core::codec::Note &note = m_all.at(m_rows.at(index.row()));
    switch (role) {
    case LuidRole:
        return int(note.luid);
    case Qt::DisplayRole:
    case TitleRole:
        return titleOf(note.body);
    case PreviewRole: {
        const qsizetype lineEnd = note.body.indexOf(u'\n');
        return lineEnd < 0 ? QString() : shortened(note.body.mid(lineEnd + 1), previewLength);
    }
    case ModifiedTextRole: {
        if (!note.lastModified.isValid())
            return QString();
        const QDateTime local = note.lastModified.toLocalTime();
        return local.date() == QDate::currentDate() ? OrganizerText::formatTime(local.time())
                                                    : OrganizerText::formatDate(local.date());
    }
    case CategoriesTextRole:
        return note.categories.join(QLatin1StringView(", "));
    default:
        break;
    }
    return {};
}

QHash<int, QByteArray> NoteListModel::roleNames() const
{
    return {
        {LuidRole, QByteArrayLiteral("luid")},
        {TitleRole, QByteArrayLiteral("title")},
        {PreviewRole, QByteArrayLiteral("preview")},
        {ModifiedTextRole, QByteArrayLiteral("modifiedText")},
        {CategoriesTextRole, QByteArrayLiteral("categoriesText")},
    };
}

void NoteListModel::setCategoryFilter(const QString &category)
{
    if (category == m_category)
        return;
    m_category = category;
    Q_EMIT categoryFilterChanged();
    refilter();
}

void NoteListModel::setNotes(const QList<core::codec::Note> &notes)
{
    const int total = totalCount();
    m_all = notes;
    refilter();
    if (total != totalCount())
        Q_EMIT countChanged();
}

void NoteListModel::setSearchText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed == m_search)
        return;
    m_search = trimmed;
    refilter();
}

const core::codec::Note *NoteListModel::find(quint32 luid) const
{
    for (const core::codec::Note &note : m_all) {
        if (note.luid == luid)
            return &note;
    }
    return nullptr;
}

QString NoteListModel::titleOf(const QString &body)
{
    const QString firstLine = body.trimmed().section(u'\n', 0, 0);
    return firstLine.isEmpty() ? tr("(Empty note)") : shortened(firstLine, titleLength);
}

int NoteListModel::rowOf(int luid) const
{
    for (qsizetype row = 0; row < m_rows.size(); ++row) {
        if (int(m_all.at(m_rows.at(row)).luid) == luid)
            return int(row);
    }
    return -1;
}

int NoteListModel::luidAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? int(m_all.at(m_rows.at(row)).luid) : 0;
}

QString NoteListModel::titleOfNote(int luid) const
{
    const core::codec::Note *note = find(quint32(luid));
    return note ? titleOf(note->body) : QString();
}

void NoteListModel::refilter()
{
    const int before = int(m_rows.size());
    beginResetModel();
    m_rows.clear();
    for (qsizetype i = 0; i < m_all.size(); ++i) {
        const core::codec::Note &note = m_all.at(i);
        if (!m_category.isEmpty() && !note.categories.contains(m_category, Qt::CaseInsensitive))
            continue;
        if (!m_search.isEmpty() && !note.body.contains(m_search, Qt::CaseInsensitive) &&
            !note.categories.join(u'\n').contains(m_search, Qt::CaseInsensitive)) {
            continue;
        }
        m_rows.append(i);
    }
    endResetModel();
    if (before != m_rows.size())
        Q_EMIT countChanged();
}

} // namespace qpm::ui
