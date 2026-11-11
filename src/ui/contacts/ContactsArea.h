#pragma once

#include "core/Error.h"
#include "core/service/ContactsService.h"
#include "core/store/ContactStore.h"
#include "ui/contacts/ContactDetailModel.h"
#include "ui/contacts/ContactEditor.h"
#include "ui/contacts/ContactListModel.h"
#include "ui/contacts/DuplicateListModel.h"

#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::store {
class ProfileStore;
}

namespace qpm::ui {

class PhotoSource;

/**
 * The contacts area of the window: the list with search and filters, the
 * contact shown, the form, the duplicates, import and export, and calling or
 * writing to a number. Every failure is reported once through
 * errorOccurred() as a translated text that says what to do.
 *
 * Owned by AppController, which hands in the profile store of the current
 * phone and the connection state. The contacts are shown without a
 * connection; changing them needs one. No call blocks; files are read and
 * written on a worker thread.
 */
class ContactsArea : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(ContactsArea)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(qpm::ui::ContactListModel *list READ list CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::ContactDetailModel *detail READ detail CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::ContactEditor *editor READ editor CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::DuplicateListModel *duplicates READ duplicates CONSTANT FINAL)
    /// The contact shown in the detail view; 0 for none.
    Q_PROPERTY(
        int currentLuid READ currentLuid WRITE setCurrentLuid NOTIFY currentLuidChanged FINAL)
    /// Narrows the list to contacts whose name, number, mail address or company holds it.
    Q_PROPERTY(
        QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged FINAL)
    /// A phone is chosen, so there is a list to show.
    Q_PROPERTY(bool hasPhone READ hasPhone NOTIFY hasPhoneChanged FINAL)
    /// Connected to the chosen phone, so contacts can be changed.
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY canEditChanged FINAL)
    /// An import, export, deletion or duplicate search runs.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// Group titles of the phone, for the group filter.
    Q_PROPERTY(QStringList groupNames READ groupNames NOTIFY metadataChanged FINAL)
    /// Accounts of the phone, readable, in the order of accountKeys.
    Q_PROPERTY(QStringList accountNames READ accountNames NOTIFY metadataChanged FINAL)
    /// Accounts as "<type>;<name>", the values of ContactListModel::accountFilter.
    Q_PROPERTY(QStringList accountKeys READ accountKeys NOTIFY metadataChanged FINAL)
    /// Position of the account that gets new contacts; -1 when unknown.
    Q_PROPERTY(int defaultAccountIndex READ defaultAccountIndex NOTIFY metadataChanged FINAL)

public:
    ContactsArea(core::protocol::PhoneConnection *connection, std::shared_ptr<PhotoSource> photos,
                 QObject *parent = nullptr);

    [[nodiscard]] ContactListModel *list() const { return m_list; }
    [[nodiscard]] ContactDetailModel *detail() const { return m_detail; }
    [[nodiscard]] ContactEditor *editor() const { return m_editor; }
    [[nodiscard]] DuplicateListModel *duplicates() const { return m_duplicates; }
    [[nodiscard]] core::service::ContactsService *service() const { return m_service; }
    [[nodiscard]] int currentLuid() const { return m_currentLuid; }
    void setCurrentLuid(int luid);
    [[nodiscard]] QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    [[nodiscard]] bool hasPhone() const { return m_store != nullptr; }
    [[nodiscard]] bool canEdit() const { return m_connected && m_store; }
    [[nodiscard]] bool isBusy() const { return m_busy > 0; }
    [[nodiscard]] QStringList groupNames() const;
    [[nodiscard]] QStringList accountNames() const;
    [[nodiscard]] QStringList accountKeys() const;
    [[nodiscard]] int defaultAccountIndex() const;

    /**
     * Shows the contacts of `profile`; null shows none. The store must stay
     * open until this is called again with another one or null.
     */
    void setProfileStore(core::store::ProfileStore *profile);
    void setConnected(bool connected);

    /// Pulls what changed on the phone, for the sync of all areas.
    QFuture<core::Result<core::store::ContactChanges>> sync();

    /// Deletes the contacts on the phone and here.
    Q_INVOKABLE bool removeContacts(const QList<int> &luids);
    /**
     * Writes the contacts `luids`, or every contact the list shows when it is
     * empty, to the local file `url`: vCard 3.0, or a table with `asTable`.
     */
    Q_INVOKABLE bool exportContacts(const QUrl &url, bool asTable, const QList<int> &luids);
    /**
     * Reads vCards or a table from the local file `url`, telling the two
     * apart by their content, and creates the contacts on the phone.
     */
    Q_INVOKABLE bool importContacts(const QUrl &url);
    /// Asks the phone to call `number`.
    Q_INVOKABLE bool call(const QString &number);
    /// Asks the window to open a new message to `number`, see messageRequested().
    Q_INVOKABLE bool writeMessage(const QString &number);
    /**
     * Fills the form for a new contact with `name` and `number`, see
     * ContactEditor::newContactWith(), and announces it through
     * newContactStarted(). False and an error without a connection.
     */
    Q_INVOKABLE bool startNewContact(const QString &name, const QString &number);
    /// Looks for duplicates among all contacts and fills `duplicates`.
    Q_INVOKABLE bool findDuplicates();
    /// Joins the second contact of the pair in `row` into the first.
    Q_INVOKABLE bool mergeDuplicate(int row);
    /// Takes the pair in `row` off the list for now.
    Q_INVOKABLE void ignoreDuplicate(int row);
    /// Makes the account at `index` of accountKeys the one for new contacts.
    Q_INVOKABLE bool setDefaultAccount(int index);

Q_SIGNALS:
    void currentLuidChanged();
    void searchTextChanged();
    void hasPhoneChanged();
    void canEditChanged();
    void busyChanged();
    void metadataChanged();
    void errorOccurred(const QString &message);
    /// A finished action the user should hear about, for example an import.
    void infoMessage(const QString &message);
    /// A message to `number` should be written; the number is already checked.
    void messageRequested(const QString &number);
    /// The duplicate search finished with `count` pairs.
    void duplicatesFound(int count);
    /// The form holds a new contact that startNewContact() filled in.
    void newContactStarted();

private:
    void reload();
    void showCurrent();
    void applySearch();
    void report(const core::Error &error);
    void beginWork();
    void endWork();

    core::service::ContactsService *m_service = nullptr;
    std::shared_ptr<PhotoSource> m_photos;
    std::shared_ptr<core::store::ContactStore> m_store;
    ContactListModel *m_list = nullptr;
    ContactDetailModel *m_detail = nullptr;
    ContactEditor *m_editor = nullptr;
    DuplicateListModel *m_duplicates = nullptr;
    core::codec::PbMetadata m_metadata;
    QString m_searchText;
    int m_currentLuid = 0;
    int m_busy = 0;
    bool m_connected = false;
};

} // namespace qpm::ui
