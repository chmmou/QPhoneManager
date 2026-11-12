#include "ui/organizer/NoteEditor.h"

#include "core/store/OrganizerStore.h"
#include "ui/organizer/OrganizerText.h"

namespace qpm::ui {

NoteEditor::NoteEditor(core::service::NotesService *service, QObject *parent)
    : QObject(parent), m_service(service)
{
    Q_ASSERT(service);
}

void NoteEditor::setStore(const std::shared_ptr<core::store::OrganizerStore> &store)
{
    m_store = store;
    reloadCategories();
    Q_EMIT canSaveChanged();
}

void NoteEditor::setConnected(bool connected)
{
    if (connected == m_connected)
        return;
    m_connected = connected;
    Q_EMIT canSaveChanged();
}

void NoteEditor::changed()
{
    Q_EMIT fieldsChanged();
    Q_EMIT canSaveChanged();
}

void NoteEditor::setBody(const QString &body)
{
    if (body == m_note.body)
        return;
    m_note.body = body;
    changed();
}

QStringList NoteEditor::categoryNames() const
{
    QStringList names = m_knownCategories;
    for (const QString &name : m_note.categories) {
        if (!names.contains(name, Qt::CaseInsensitive))
            names.append(name);
    }
    return names;
}

bool NoteEditor::isModified() const
{
    return isNew() ? !m_note.body.trimmed().isEmpty()
                   : !core::codec::sameNoteContent(m_note, m_original);
}

bool NoteEditor::canSave() const
{
    return m_connected && !m_busy && !m_store.expired() && !m_note.body.trimmed().isEmpty();
}

QString NoteEditor::summaryText(const core::codec::Note &note)
{
    QStringList lines{note.body.trimmed()};
    if (!note.categories.isEmpty())
        lines.append(tr("Categories: %1").arg(note.categories.join(QLatin1StringView(", "))));
    return lines.join(u'\n');
}

QString NoteEditor::ownVersionText() const
{
    return m_conflict ? summaryText(m_note) : QString();
}

QString NoteEditor::phoneVersionText() const
{
    return m_conflict ? summaryText(m_conflict->phoneVersion) : QString();
}

void NoteEditor::reloadCategories()
{
    m_knownCategories.clear();
    if (const std::shared_ptr<core::store::OrganizerStore> store = m_store.lock()) {
        const core::Result<QList<core::codec::OrganizerCategory>> categories =
            store->noteCategories();
        if (categories.hasValue()) {
            for (const core::codec::OrganizerCategory &category : categories.value())
                m_knownCategories.append(category.name);
        }
    }
    Q_EMIT fieldsChanged();
}

void NoteEditor::show(const core::codec::Note &note)
{
    m_original = note;
    m_note = note;
    const bool hadConflict = m_conflict.has_value();
    m_conflict.reset();
    Q_EMIT loaded();
    changed();
    if (hadConflict)
        Q_EMIT conflictChanged();
}

void NoteEditor::newNote()
{
    reloadCategories();
    show(core::codec::Note());
}

bool NoteEditor::load(int luid)
{
    const std::shared_ptr<core::store::OrganizerStore> store = m_store.lock();
    const core::Result<core::codec::Note> note =
        store && luid > 0 ? store->note(quint32(luid))
                          : core::Result<core::codec::Note>(
                                core::Error(core::ErrorCode::NotFound, QStringLiteral("No note")));
    if (!note.hasValue()) {
        Q_EMIT errorOccurred(OrganizerText::errorText(note.error(), OrganizerText::Area::Notes));
        return false;
    }
    reloadCategories();
    show(note.value());
    return true;
}

void NoteEditor::setCategorySelected(const QString &name, bool selected)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || m_note.categories.contains(trimmed, Qt::CaseInsensitive) == selected)
        return;
    if (selected) {
        m_note.categories.append(trimmed);
    } else {
        m_note.categories.removeIf([&trimmed](const QString &category) {
            return category.compare(trimmed, Qt::CaseInsensitive) == 0;
        });
    }
    changed();
}

bool NoteEditor::addCategory(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;
    setCategorySelected(trimmed, true);
    return true;
}

void NoteEditor::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
    Q_EMIT canSaveChanged();
}

bool NoteEditor::save()
{
    if (m_busy)
        return false;
    if (m_note.body.trimmed().isEmpty()) {
        Q_EMIT errorOccurred(OrganizerText::errorText(
            core::Error(core::ErrorCode::InvalidInput, QStringLiteral("Empty note")),
            OrganizerText::Area::Notes));
        return false;
    }
    if (!m_connected) {
        Q_EMIT errorOccurred(OrganizerText::errorText(
            core::Error(core::ErrorCode::ConnectionClosed, QStringLiteral("Not connected")),
            OrganizerText::Area::Notes));
        return false;
    }
    startSave(core::service::SaveMode::CheckConflict);
    return true;
}

void NoteEditor::startSave(core::service::SaveMode mode)
{
    setBusy(true);
    m_service->save(m_note, mode)
        .then(this, [this](const core::Result<core::service::NoteSaveOutcome> &outcome) {
            setBusy(false);
            if (!outcome.hasValue()) {
                const QString message =
                    OrganizerText::errorText(outcome.error(), OrganizerText::Area::Notes);
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

bool NoteEditor::keepOwnVersion()
{
    if (!m_conflict || m_busy)
        return false;
    m_conflict.reset();
    Q_EMIT conflictChanged();
    startSave(core::service::SaveMode::Overwrite);
    return true;
}

bool NoteEditor::takePhoneVersion()
{
    if (!m_conflict || m_busy)
        return false;
    const core::Result<void> kept = m_service->keepPhoneVersion(m_conflict->phoneObject);
    if (!kept.hasValue()) {
        Q_EMIT errorOccurred(OrganizerText::errorText(kept.error(), OrganizerText::Area::Notes));
        return false;
    }
    const int luid = int(m_original.luid);
    static_cast<void>(load(luid));
    Q_EMIT saved(luid);
    return true;
}

} // namespace qpm::ui
