#pragma once

#include "core/Error.h"
#include "core/store/OrganizerStore.h"
#include "ui/organizer/CategoryModel.h"
#include "ui/organizer/NoteEditor.h"
#include "ui/organizer/NoteListModel.h"

#include <QFuture>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::service {
class NotesService;
}

namespace qpm::ui {

/**
 * The notes area of the window: the list with search and a category filter,
 * the form of the note chosen and the note categories with their colours.
 * Every failure is reported once through errorOccurred() as a translated text
 * that says what to do.
 *
 * Owned by AppController, which hands in the store and the connection state.
 * The notes are shown without a connection; changing them needs one. No call
 * blocks.
 */
class NotesArea : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(NotesArea)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(qpm::ui::NoteListModel *list READ list CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::NoteEditor *editor READ editor CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::CategoryModel *categories READ categories CONSTANT FINAL)
    /// The note in the form; 0 for none or a new one.
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
    explicit NotesArea(core::protocol::PhoneConnection *connection, QObject *parent = nullptr);

    [[nodiscard]] NoteListModel *list() const { return m_list; }
    [[nodiscard]] NoteEditor *editor() const { return m_editor; }
    [[nodiscard]] CategoryModel *categories() const { return m_categories; }
    [[nodiscard]] core::service::NotesService *service() const { return m_service; }
    [[nodiscard]] int currentLuid() const { return m_currentLuid; }
    void setCurrentLuid(int luid);
    [[nodiscard]] QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    [[nodiscard]] bool hasPhone() const { return m_store != nullptr; }
    [[nodiscard]] bool canEdit() const { return m_connected && m_store; }
    [[nodiscard]] bool isBusy() const { return m_busy > 0; }
    [[nodiscard]] QStringList colorChoices() const;

    /// Shows the notes of `store`, which CalendarArea shares; null shows none.
    void setStore(const std::shared_ptr<core::store::OrganizerStore> &store);
    void setConnected(bool connected);

    /// Pulls what changed in the notes, for the sync of all areas.
    QFuture<core::Result<core::store::OrganizerChanges>> sync();

    /// Empties the form for a new note.
    Q_INVOKABLE void startNew();
    Q_INVOKABLE bool removeNote(int luid);
    /// Adds a note category with the colour at `colorIndex` of colorChoices.
    Q_INVOKABLE bool addCategory(const QString &name, int colorIndex);
    /// Renames and recolours the category in `row` of categories.
    Q_INVOKABLE bool editCategory(int row, const QString &name, int colorIndex);
    /// Deletes the category in `row`.
    Q_INVOKABLE bool removeCategory(int row);

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

    core::service::NotesService *m_service = nullptr;
    std::shared_ptr<core::store::OrganizerStore> m_store;
    NoteListModel *m_list = nullptr;
    NoteEditor *m_editor = nullptr;
    CategoryModel *m_categories = nullptr;
    QString m_searchText;
    int m_currentLuid = 0;
    int m_busy = 0;
    bool m_connected = false;
};

} // namespace qpm::ui
