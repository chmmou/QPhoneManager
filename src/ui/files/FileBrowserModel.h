#pragma once

#include "core/Error.h"
#include "core/codec/FolderListing.h"
#include "ui/files/TransferQueue.h"

#include <QAbstractListModel>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <memory>

namespace qpm::core::service {
class FileService;
class ThumbnailChannel;
} // namespace qpm::core::service

namespace qpm::ui {

class PhotoSource;

/**
 * The files of the connected phone for the window: one folder at a time,
 * folders first, with thumbnails of pictures and videos, and the actions on
 * its entries. Copies go through the transfer queue.
 *
 * Paths are those of core::service::FileService; the empty path is the root
 * with the storages of the phone. Nothing is kept on this computer: without
 * a connection the list is empty. Every failure is reported once through
 * errorOccurred() as a translated text that says what to do; an action that
 * cannot start returns false.
 *
 * Lives in the main thread; no call blocks. The services and `photos` must
 * outlive the model.
 */
class FileBrowserModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(FileBrowserModel)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(QString path READ path NOTIFY pathChanged FINAL)
    /// The folders from the root to the one shown, translated for storages.
    Q_PROPERTY(QStringList crumbNames READ crumbNames NOTIFY pathChanged FINAL)
    /// The paths belonging to crumbNames.
    Q_PROPERTY(QStringList crumbPaths READ crumbPaths NOTIFY pathChanged FINAL)
    Q_PROPERTY(bool atRoot READ atRoot NOTIFY pathChanged FINAL)
    /// Connected, so the files can be shown and changed.
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY canEditChanged FINAL)
    /// A listing or a change on the phone runs.
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged FINAL)
    /// Why the list is empty when it is: no connection, an empty folder, a failure.
    Q_PROPERTY(QString emptyText READ emptyText NOTIFY emptyTextChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// Entries were cut and can be put into the folder shown.
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY canPasteChanged FINAL)
    Q_PROPERTY(qpm::ui::TransferQueue *transfers READ transfers CONSTANT FINAL)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        DisplayNameRole,
        IsFolderRole,
        SizeRole,
        SizeTextRole,
        ModifiedRole,
        ModifiedTextRole,
        DetailsRole,
        ThumbnailUrlRole,
        IconNameRole,
    };

    FileBrowserModel(core::service::FileService *service,
                     core::service::ThumbnailChannel *thumbnails, TransferQueue *transfers,
                     std::shared_ptr<PhotoSource> photos, QObject *parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] QStringList crumbNames() const;
    [[nodiscard]] QStringList crumbPaths() const;
    [[nodiscard]] bool atRoot() const { return m_path.isEmpty(); }
    [[nodiscard]] bool canEdit() const { return m_connected; }
    [[nodiscard]] bool isBusy() const { return m_busy > 0; }
    [[nodiscard]] QString emptyText() const { return m_emptyText; }
    [[nodiscard]] int count() const { return int(m_entries.size()); }
    [[nodiscard]] bool canPaste() const { return m_connected && !m_cut.isEmpty(); }
    [[nodiscard]] TransferQueue *transfers() const { return m_transfers; }

    /**
     * Starts at the root after a new connection and empties the list without
     * one. Nothing is read from the phone before load().
     */
    void setConnected(bool connected);

    /**
     * Lists the folder shown unless that happened already in this
     * connection; the window calls it when the files are shown.
     */
    Q_INVOKABLE bool load();

    /// Shows the folder `path` and lists it.
    Q_INVOKABLE bool open(const QString &path);
    /// Opens the folder in row `row`, or opens its file on the desktop.
    Q_INVOKABLE bool openRow(int row);
    /// Shows the folder above the one shown.
    Q_INVOKABLE bool up();
    /// Lists the folder shown again.
    Q_INVOKABLE bool refresh();

    /// Copies the entries `names` into the local folder `folder`.
    Q_INVOKABLE bool download(const QStringList &names, const QUrl &folder);
    /// Copies the local files and folders `urls` into the folder shown.
    Q_INVOKABLE bool upload(const QList<QUrl> &urls);
    /// Creates the folder `name` in the folder shown.
    Q_INVOKABLE bool mkdir(const QString &name);
    Q_INVOKABLE bool rename(const QString &oldName, const QString &newName);
    /// Deletes the entries `names` on the phone, folders with all they hold.
    Q_INVOKABLE bool remove(const QStringList &names);
    /// Copies the file `name` to a temporary folder and opens it with the desktop's program.
    Q_INVOKABLE bool openOnDesktop(const QString &name);
    /// Remembers the entries `names` to move them with paste().
    Q_INVOKABLE bool cut(const QStringList &names);
    /// Moves the cut entries into the folder shown.
    Q_INVOKABLE bool paste();
    /// Copies the files `names` to a temporary folder; dragReady() follows.
    Q_INVOKABLE bool prepareDrag(const QStringList &names);

    [[nodiscard]] Q_INVOKABLE int rowOf(const QString &name) const;
    [[nodiscard]] Q_INVOKABLE QString nameAt(int row) const;
    [[nodiscard]] Q_INVOKABLE bool isFolderAt(int row) const;
    /**
     * Why `name` cannot be the new name of an entry in the folder shown, as a
     * translated text; empty when it can. `oldName` is the entry renamed, if any.
     */
    [[nodiscard]] Q_INVOKABLE QString nameProblem(const QString &name,
                                                  const QString &oldName = {}) const;

    /// The translated name of a storage such as "sdcard"; other names stay.
    [[nodiscard]] static QString storageName(const QString &name);

Q_SIGNALS:
    void pathChanged();
    void canEditChanged();
    void busyChanged();
    void emptyTextChanged();
    void countChanged();
    void canPasteChanged();
    void errorOccurred(const QString &message);
    void infoMessage(const QString &message);
    /// Local copies of the files asked for by prepareDrag() are at `urls`.
    void dragReady(const QList<QUrl> &urls);

private:
    void list();
    void setEntries(QList<core::codec::FolderEntry> entries);
    void setEmptyText(const QString &text);
    void fetchThumbnails(quint64 generation, QStringList names);
    void runOneAfterAnother(QStringList paths,
                            std::function<QFuture<core::Result<void>>(const QString &)> step,
                            const QString &doneText);
    void beginWork();
    void endWork();
    [[nodiscard]] QList<core::codec::FolderEntry> entriesNamed(const QStringList &names) const;
    [[nodiscard]] QStringList names() const;

    core::service::FileService *m_service = nullptr;
    core::service::ThumbnailChannel *m_thumbnails = nullptr;
    TransferQueue *m_transfers = nullptr;
    std::shared_ptr<PhotoSource> m_photos;
    QList<core::codec::FolderEntry> m_entries;
    QHash<QString, quint64> m_thumbnailIds;
    QStringList m_cut;
    QString m_path;
    QString m_emptyText;
    quint64 m_generation = 0;
    int m_busy = 0;
    bool m_connected = false;
    bool m_loaded = false;
};

} // namespace qpm::ui
