#pragma once

#include "core/codec/VNote.h"
#include "core/service/NotesService.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

namespace qpm::core::store {
class OrganizerStore;
}

namespace qpm::ui {

/**
 * The form for one note, new or loaded from the store: its text and its
 * categories. A category typed here that the phone does not know yet is
 * created there with the note.
 *
 * save() sends the note. When the note changed on the phone since it was
 * loaded, the save stops, hasConflict turns true and both versions are shown;
 * keepOwnVersion() or takePhoneVersion() decides.
 *
 * Owned by NotesArea, which hands in the store and the connection state.
 * Saving needs a connection; everything else works without one.
 */
class NoteEditor : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NoteEditor)
    QML_UNCREATABLE("Provided by NotesArea")

    Q_PROPERTY(bool isNew READ isNew NOTIFY loaded FINAL)
    Q_PROPERTY(int luid READ luid NOTIFY loaded FINAL)
    Q_PROPERTY(QString body READ body WRITE setBody NOTIFY fieldsChanged FINAL)
    /// The categories of the phone and those the note names, by name.
    Q_PROPERTY(QStringList categoryNames READ categoryNames NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QStringList selectedCategories READ selectedCategories NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(bool modified READ isModified NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// Connected, not busy and the note holds text.
    Q_PROPERTY(bool canSave READ canSave NOTIFY canSaveChanged FINAL)
    Q_PROPERTY(bool hasConflict READ hasConflict NOTIFY conflictChanged FINAL)
    Q_PROPERTY(QString ownVersionText READ ownVersionText NOTIFY conflictChanged FINAL)
    Q_PROPERTY(QString phoneVersionText READ phoneVersionText NOTIFY conflictChanged FINAL)

public:
    /// `service` must outlive this object.
    explicit NoteEditor(core::service::NotesService *service, QObject *parent = nullptr);

    void setStore(const std::shared_ptr<core::store::OrganizerStore> &store);
    void setConnected(bool connected);

    [[nodiscard]] bool isNew() const { return m_original.luid == 0; }
    [[nodiscard]] int luid() const { return int(m_original.luid); }
    [[nodiscard]] QString body() const { return m_note.body; }
    void setBody(const QString &body);
    [[nodiscard]] QStringList categoryNames() const;
    [[nodiscard]] QStringList selectedCategories() const { return m_note.categories; }
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] bool isBusy() const { return m_busy; }
    [[nodiscard]] bool canSave() const;
    [[nodiscard]] bool hasConflict() const { return m_conflict.has_value(); }
    [[nodiscard]] QString ownVersionText() const;
    [[nodiscard]] QString phoneVersionText() const;

    /// Empties the form for a new note.
    Q_INVOKABLE void newNote();
    /// Fills the form with the stored note `luid`; false and an error when it is unknown.
    Q_INVOKABLE bool load(int luid);
    /// Starts saving; false and an error when the form cannot be saved now.
    Q_INVOKABLE bool save();
    /// After a conflict: writes this form over the phone's version.
    Q_INVOKABLE bool keepOwnVersion();
    /// After a conflict: drops this form and keeps the phone's version.
    Q_INVOKABLE bool takePhoneVersion();
    Q_INVOKABLE void setCategorySelected(const QString &name, bool selected);
    /// Adds a category to this note; the phone creates it on saving. False for an empty name.
    Q_INVOKABLE bool addCategory(const QString &name);

    /// The note as the form holds it.
    [[nodiscard]] core::codec::Note edited() const { return m_note; }

Q_SIGNALS:
    void loaded();
    void fieldsChanged();
    void busyChanged();
    void canSaveChanged();
    void conflictChanged();
    /// The note `luid` is on the phone and in the store as the form showed it.
    void saved(int luid);
    void errorOccurred(const QString &message);

private:
    void show(const core::codec::Note &note);
    void changed();
    void setBusy(bool busy);
    void startSave(core::service::SaveMode mode);
    void reloadCategories();
    [[nodiscard]] static QString summaryText(const core::codec::Note &note);

    core::service::NotesService *m_service = nullptr;
    std::weak_ptr<core::store::OrganizerStore> m_store;
    core::codec::Note m_original;
    core::codec::Note m_note;
    QStringList m_knownCategories;
    std::optional<core::service::NoteSaveOutcome> m_conflict;
    bool m_connected = false;
    bool m_busy = false;
};

} // namespace qpm::ui
