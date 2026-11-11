#pragma once

#include "core/Error.h"
#include "core/service/CallLogService.h"
#include "core/store/CallStore.h"
#include "ui/calls/CallListModel.h"

#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

namespace qpm::core::protocol {
class PhoneConnection;
}

namespace qpm::core::service {
class ContactsService;
}

namespace qpm::core::store {
class ContactStore;
class ProfileStore;
} // namespace qpm::core::store

namespace qpm::ui {

/**
 * The call log area of the window: the list with its kind filter and search,
 * the entry shown, deleting, export, calling back, writing a message and
 * taking a number into the contacts. Every failure is reported once through
 * errorOccurred() as a translated text that says what to do.
 *
 * Owned by AppController, which hands in the profile store of the current
 * phone and the connection state. The call log is shown without a
 * connection; deleting, calling and the sync need one. No call blocks; the
 * export file is written on a worker thread.
 */
class CallsArea : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(CallsArea)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(qpm::ui::CallListModel *list READ list CONSTANT FINAL)
    /// The entry shown in the detail view; 0 for none.
    Q_PROPERTY(int currentId READ currentId WRITE setCurrentId NOTIFY currentIdChanged FINAL)
    /// Narrows the list to entries whose name or number holds it.
    Q_PROPERTY(
        QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged FINAL)
    /// A phone is chosen, so there is a list to show.
    Q_PROPERTY(bool hasPhone READ hasPhone NOTIFY hasPhoneChanged FINAL)
    /// Connected to the chosen phone, so entries can be deleted and numbers called.
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY canEditChanged FINAL)
    /// A deletion or an export runs.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    Q_PROPERTY(bool hasCurrent READ hasCurrent NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY currentChanged FINAL)
    /// The number as the phone keeps it; empty for a hidden number.
    Q_PROPERTY(QString currentNumber READ currentNumber NOTIFY currentChanged FINAL)
    Q_PROPERTY(
        qpm::ui::CallListModel::Kind currentKind READ currentKind NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentKindText READ currentKindText NOTIFY currentChanged FINAL)
    /// Date and time in the long form of the current locale.
    Q_PROPERTY(QString currentTimeText READ currentTimeText NOTIFY currentChanged FINAL)
    /// Empty for a missed call.
    Q_PROPERTY(QString currentDurationText READ currentDurationText NOTIFY currentChanged FINAL)
    /// "SIM 1" and so on; empty when the phone does not say.
    Q_PROPERTY(QString currentSimText READ currentSimText NOTIFY currentChanged FINAL)
    /// A stored contact has the number of the entry shown.
    Q_PROPERTY(bool currentInContacts READ currentInContacts NOTIFY currentChanged FINAL)
    /// The number of the entry shown can be called and written to.
    Q_PROPERTY(bool currentCallable READ currentCallable NOTIFY currentChanged FINAL)

public:
    /**
     * `dialer` places calls for call(); it must outlive this object.
     * `connection` must too.
     */
    CallsArea(core::protocol::PhoneConnection *connection, core::service::ContactsService *dialer,
              QObject *parent = nullptr);

    [[nodiscard]] CallListModel *list() const { return m_list; }
    [[nodiscard]] core::service::CallLogService *service() const { return m_service; }
    [[nodiscard]] int currentId() const { return m_currentId; }
    void setCurrentId(int id);
    [[nodiscard]] QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    [[nodiscard]] bool hasPhone() const { return m_store != nullptr; }
    [[nodiscard]] bool canEdit() const { return m_connected && m_store; }
    [[nodiscard]] bool isBusy() const { return m_busy > 0; }
    [[nodiscard]] bool hasCurrent() const { return m_current.has_value(); }
    [[nodiscard]] QString currentTitle() const;
    [[nodiscard]] QString currentNumber() const;
    [[nodiscard]] CallListModel::Kind currentKind() const;
    [[nodiscard]] QString currentKindText() const;
    [[nodiscard]] QString currentTimeText() const;
    [[nodiscard]] QString currentDurationText() const;
    [[nodiscard]] QString currentSimText() const;
    [[nodiscard]] bool currentInContacts() const;
    [[nodiscard]] bool currentCallable() const;

    /**
     * Shows the call log of `profile`; null shows none. The store must stay
     * open until this is called again with another one or null.
     */
    void setProfileStore(core::store::ProfileStore *profile);
    void setConnected(bool connected);
    /// Reads the contact names again, after the contacts changed.
    void refreshNames();

    /// Reads the call log from the phone, for the sync of all areas.
    QFuture<core::Result<core::store::CallChanges>> sync();

    /// Deletes the entries `ids` on the phone and here.
    Q_INVOKABLE bool removeCalls(const QList<int> &ids);
    /// The ids of every entry the list shows.
    [[nodiscard]] Q_INVOKABLE QList<int> shownIds() const;
    /// Writes the entries the list shows to the local file `url` as a table.
    Q_INVOKABLE bool exportCalls(const QUrl &url);
    /// Asks the phone to call `number`.
    Q_INVOKABLE bool call(const QString &number);
    /// Asks the window to open a new message to `number`, see messageRequested().
    Q_INVOKABLE bool writeMessage(const QString &number);
    /// Asks the window to open a new contact with the number of the entry shown.
    Q_INVOKABLE bool addCurrentToContacts();

Q_SIGNALS:
    void currentIdChanged();
    void currentChanged();
    void searchTextChanged();
    void hasPhoneChanged();
    void canEditChanged();
    void busyChanged();
    void errorOccurred(const QString &message);
    /// A finished action the user should hear about, for example an export.
    void infoMessage(const QString &message);
    /// A message to `number` should be written; the number is already checked.
    void messageRequested(const QString &number);
    /// A new contact with `name` and `number` should be opened; the number is checked.
    void newContactRequested(const QString &name, const QString &number);

private:
    void reload();
    void showCurrent();
    void report(const core::Error &error);
    void beginWork();
    void endWork();

    core::service::CallLogService *m_service = nullptr;
    core::service::ContactsService *m_dialer = nullptr;
    std::shared_ptr<core::store::CallStore> m_store;
    std::shared_ptr<core::store::ContactStore> m_contacts;
    CallListModel *m_list = nullptr;
    std::optional<core::codec::CallEntry> m_current;
    QString m_searchText;
    int m_currentId = 0;
    int m_busy = 0;
    bool m_connected = false;
};

} // namespace qpm::ui
