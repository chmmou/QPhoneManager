#pragma once

#include "core/Error.h"
#include "core/codec/AppListCodec.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

namespace qpm::core::service {
class AppsService;
class FileService;
} // namespace qpm::core::service

namespace qpm::ui {

class PhotoSource;

/**
 * The apps of the connected phone for the window, sorted by name, narrowed
 * by the kind of app and by the search, with their icons and the actions on
 * them: start, ask for the removal, save the APK on this computer and install
 * an APK from it.
 *
 * Nothing is kept on this computer: the list is read from the phone once it
 * is shown in a connection and is empty without one. A removal or an
 * installation only starts on the phone; the phone asks its user there, and
 * the list follows what the phone then reports. One copy runs at a time,
 * shared with the file browser.
 *
 * Every failure is reported once through errorOccurred() as a translated text
 * that says what to do; an action that cannot start returns false. Lives in
 * the main thread; no call blocks. The services and `photos` must outlive the
 * model.
 */
class AppListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AppListModel)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Apps of the phone before any filter.
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged FINAL)
    Q_PROPERTY(Filter filter READ filter WRITE setFilter NOTIFY filterChanged FINAL)
    /// Shows only apps whose name or package holds the text, without regard to case.
    Q_PROPERTY(
        QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged FINAL)
    /// Connected, so the apps can be shown and changed.
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY canEditChanged FINAL)
    /// The list is read from the phone.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// Why the list is empty when it is: no connection, no apps, nothing matches, a failure.
    Q_PROPERTY(QString emptyText READ emptyText NOTIFY emptyTextChanged FINAL)
    /// A copy of an APK runs.
    Q_PROPERTY(bool transferActive READ isTransferActive NOTIFY transferChanged FINAL)
    /// 0 to 1 over the running copy.
    Q_PROPERTY(double transferProgress READ transferProgress NOTIFY transferChanged FINAL)
    /// What the running copy does and how far it is; empty while none runs.
    Q_PROPERTY(QString transferText READ transferText NOTIFY transferChanged FINAL)

public:
    enum class Filter { User, System, All };
    Q_ENUM(Filter)

    enum Role {
        /// Named "packageName", since QML takes "package" for a reserved word.
        PackageRole = Qt::UserRole + 1,
        LabelRole,
        VersionRole,
        /// Bytes of the APK, 0 when not known.
        SizeRole,
        SizeTextRole,
        /// When the APK was installed or last updated; invalid when not known.
        InstalledRole,
        InstalledTextRole,
        /// "image://qpm/app/<package>" once the icon is there, else empty.
        IconUrlRole,
        IsUserAppRole,
        /// The phone named the APK, so it can be saved.
        CanBackupRole,
        /// Version, size and date in one line, for the description read out.
        DetailsRole,
    };

    AppListModel(core::service::AppsService *service, core::service::FileService *files,
                 std::shared_ptr<PhotoSource> photos, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return int(m_rows.size()); }
    [[nodiscard]] int totalCount() const { return int(m_apps.size()); }
    [[nodiscard]] Filter filter() const { return m_filter; }
    void setFilter(Filter filter);
    [[nodiscard]] QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);
    [[nodiscard]] bool canEdit() const { return m_connected; }
    [[nodiscard]] bool isBusy() const { return m_busy; }
    [[nodiscard]] QString emptyText() const;
    [[nodiscard]] bool isTransferActive() const { return m_transfer != Transfer::None; }
    [[nodiscard]] double transferProgress() const;
    [[nodiscard]] QString transferText() const;

    /**
     * Empties the list without a connection and after a new one; nothing is
     * read from the phone before load().
     */
    void setConnected(bool connected);

    /// Reads the apps unless that happened already in this connection; the window calls it when the
    /// apps are shown.
    Q_INVOKABLE bool load();
    /// Reads the apps again.
    Q_INVOKABLE bool refresh();
    /// Opens the app on the phone.
    Q_INVOKABLE bool start(const QString &package);
    /// Makes the phone ask its user whether to remove the app.
    Q_INVOKABLE bool uninstall(const QString &package);
    /// Saves the APK of the app as the local file `file`, which is replaced when it exists.
    Q_INVOKABLE bool backup(const QString &package, const QUrl &file);
    /// Hands the local APK `file` to the phone, which then asks its user whether to install it.
    Q_INVOKABLE bool install(const QUrl &file);
    /// Stops the running copy.
    Q_INVOKABLE void cancelTransfer();

    /// The row of `package`, or -1 when it is not shown.
    [[nodiscard]] Q_INVOKABLE int rowOf(const QString &package) const;
    /// The package in `row`, or an empty text.
    [[nodiscard]] Q_INVOKABLE QString packageAt(int row) const;
    /// The name of the app `package`, also when it is filtered out; empty when unknown.
    [[nodiscard]] Q_INVOKABLE QString labelOf(const QString &package) const;
    /// Whether the APK of `package` can be saved.
    [[nodiscard]] Q_INVOKABLE bool canBackup(const QString &package) const;
    /// A file name for the APK of `package`, such as "Example-1.2.apk".
    [[nodiscard]] Q_INVOKABLE QString backupFileName(const QString &package) const;

    /// A translated text for a failure: what happened and what to do.
    [[nodiscard]] static QString messageFor(const core::Error &error);

Q_SIGNALS:
    void countChanged();
    void filterChanged();
    void searchTextChanged();
    void canEditChanged();
    void busyChanged();
    void emptyTextChanged();
    void transferChanged();
    void errorOccurred(const QString &message);
    void infoMessage(const QString &message);

private:
    enum class Transfer { None, Backup, Install };

    void readApps();
    void setApps(QList<core::codec::AppInfo> apps);
    void refilter();
    void fetchIcons(quint64 generation, QStringList packages);
    void onAppAdded(const core::codec::AppInfo &app);
    void onAppRemoved(const QString &package);
    void setBusy(bool busy);
    void beginTransfer(Transfer kind, const QString &label);
    void endTransfer();
    [[nodiscard]] bool matches(const core::codec::AppInfo &app) const;
    [[nodiscard]] const core::codec::AppInfo *app(const QString &package) const;
    [[nodiscard]] bool canStartTransfer();

    core::service::AppsService *m_service = nullptr;
    core::service::FileService *m_files = nullptr;
    std::shared_ptr<PhotoSource> m_photos;
    /// Sorted by label.
    QList<core::codec::AppInfo> m_apps;
    /// Indexes into m_apps of the rows shown.
    QList<qsizetype> m_rows;
    /// Packages whose icon is kept in the photo source.
    QSet<QString> m_withIcon;
    /// Packages whose removal the phone asks about.
    QSet<QString> m_pendingRemovals;
    QString m_searchText;
    QString m_failureText;
    QString m_transferLabel;
    Filter m_filter = Filter::User;
    Transfer m_transfer = Transfer::None;
    quint64 m_transferDone = 0;
    quint64 m_transferTotal = 0;
    quint64 m_generation = 0;
    bool m_connected = false;
    bool m_loaded = false;
    bool m_busy = false;
    /// An APK went to the phone and its installation is not reported yet.
    bool m_awaitingInstall = false;
};

} // namespace qpm::ui
