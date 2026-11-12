#include "ui/organizer/NotesArea.h"

#include "core/service/NotesService.h"
#include "ui/organizer/OrganizerText.h"

namespace qpm::ui {

namespace {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::codec::CategoryChange;

} // namespace

NotesArea::NotesArea(core::protocol::PhoneConnection *connection, QObject *parent)
    : QObject(parent), m_service(new core::service::NotesService(connection, this)),
      m_list(new NoteListModel(this)), m_editor(new NoteEditor(m_service, this)),
      m_categories(new CategoryModel(this))
{
    connect(m_service, &core::service::NotesService::notesChanged, this, &NotesArea::reload);
    connect(m_editor, &NoteEditor::errorOccurred, this, &NotesArea::errorOccurred);
    connect(m_editor, &NoteEditor::saved, this, [this](int luid) { setCurrentLuid(luid); });
    m_editor->newNote();
}

void NotesArea::setCurrentLuid(int luid)
{
    if (luid == m_currentLuid)
        return;
    m_currentLuid = luid;
    if (luid > 0 && m_editor->luid() != luid)
        static_cast<void>(m_editor->load(luid));
    Q_EMIT currentLuidChanged();
}

void NotesArea::setSearchText(const QString &text)
{
    if (text == m_searchText)
        return;
    m_searchText = text;
    m_list->setSearchText(text);
    Q_EMIT searchTextChanged();
}

QStringList NotesArea::colorChoices() const
{
    return OrganizerText::colorChoices().mid(1);
}

void NotesArea::setStore(const std::shared_ptr<core::store::OrganizerStore> &store)
{
    m_store = store;
    m_service->setStore(store);
    m_editor->setStore(store);
    m_editor->setConnected(canEdit());
    m_currentLuid = 0;
    m_editor->newNote();
    reload();
    Q_EMIT currentLuidChanged();
    Q_EMIT hasPhoneChanged();
    Q_EMIT canEditChanged();
}

void NotesArea::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    m_editor->setConnected(canEdit());
    Q_EMIT canEditChanged();
}

QFuture<Result<core::store::OrganizerChanges>> NotesArea::sync()
{
    return m_service->sync();
}

void NotesArea::reload()
{
    if (!m_store) {
        m_list->setNotes({});
        m_categories->setCategories({});
        return;
    }
    const Result<QList<core::codec::Note>> notes = m_store->notes();
    if (!notes.hasValue())
        report(notes.error());
    m_list->setNotes(notes.hasValue() ? notes.value() : QList<core::codec::Note>());
    const Result<QList<core::codec::OrganizerCategory>> categories = m_store->noteCategories();
    m_categories->setCategories(categories.hasValue() ? categories.value()
                                                      : QList<core::codec::OrganizerCategory>());
    m_editor->setStore(m_store);
    if (m_currentLuid > 0 && !m_list->find(quint32(m_currentLuid))) {
        m_currentLuid = 0;
        m_editor->newNote();
        Q_EMIT currentLuidChanged();
    }
}

void NotesArea::report(const Error &error)
{
    const QString message = OrganizerText::errorText(error, OrganizerText::Area::Notes);
    if (!message.isEmpty())
        Q_EMIT errorOccurred(message);
}

void NotesArea::beginWork()
{
    if (m_busy++ == 0)
        Q_EMIT busyChanged();
}

void NotesArea::endWork()
{
    if (m_busy > 0 && --m_busy == 0)
        Q_EMIT busyChanged();
}

void NotesArea::startNew()
{
    m_editor->newNote();
    if (m_currentLuid != 0) {
        m_currentLuid = 0;
        Q_EMIT currentLuidChanged();
    }
}

bool NotesArea::removeNote(int luid)
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

void NotesArea::changeCategories(const QList<CategoryChange> &changes, const QString &done)
{
    beginWork();
    m_service->changeCategories(changes).then(this, [this, done](const Result<void> &changed) {
        endWork();
        if (!changed.hasValue()) {
            report(changed.error());
            return;
        }
        Q_EMIT infoMessage(done);
    });
}

bool NotesArea::addCategory(const QString &name, int colorIndex)
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

bool NotesArea::editCategory(int row, const QString &name, int colorIndex)
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

bool NotesArea::removeCategory(int row)
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

} // namespace qpm::ui
