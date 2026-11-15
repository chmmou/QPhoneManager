#pragma once

#include "core/Error.h"
#include "core/store/ProfileStore.h"
#include "ui/DeviceStatusModel.h"
#include "ui/FoundPhoneModel.h"
#include "ui/ProfileListModel.h"
#include "ui/QrCodeProvider.h"
#include "ui/apps/AppListModel.h"
#include "ui/backup/BackupController.h"
#include "ui/calls/CallsArea.h"
#include "ui/contacts/ContactsArea.h"
#include "ui/desktop/CallControl.h"
#include "ui/desktop/NotificationListModel.h"
#include "ui/files/FileBrowserModel.h"
#include "ui/files/TransferQueue.h"
#include "ui/messages/MessagesArea.h"
#include "ui/organizer/CalendarArea.h"
#include "ui/organizer/NotesArea.h"
#include "ui/organizer/TasksArea.h"
#include "ui/screen/ScreenMirror.h"

#include <QFuture>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <chrono>
#include <functional>
#include <memory>

class QJSEngine;
class QQmlEngine;

namespace qpm {
class BluetoothDeviceFinder;
class DesktopNotifier;
class SecretStore;
} // namespace qpm

namespace qpm::core::protocol {
class AdbBridge;
class Discovery;
class PhoneConnection;
} // namespace qpm::core::protocol

namespace qpm::core::service {
class AppsService;
class CallMonitor;
class ClipboardService;
class DeviceStatusService;
class FileService;
class NotificationService;
class ScreenMirrorClient;
class ThumbnailChannel;
} // namespace qpm::core::service

namespace qpm::core::store {
class ContactStore;
}

namespace qpm::ui {

class DesktopIntegration;
class Settings;

/**
 * Runs the program's connection to the phone and the phone profiles for the
 * user interface: connecting over Wi-Fi, USB, Bluetooth or the QR code, the
 * PIN, the search for phones, the profiles stored on this computer and the
 * sync of all areas. Lives in the main thread; no call blocks, results arrive
 * as property changes and signals.
 *
 * Every failure is reported once through errorOccurred() as a translated text
 * that says what went wrong and what to do. While reconnecting after a lost
 * connection, failed attempts stay quiet.
 *
 * The object owns the connection, the models, the areas, the file browser
 * with its transfers, the apps, the screen, the notifications, the call, the
 * clipboard sync, the backup and the profile store of the current phone. The
 * contacts, the call log, the messages, the calendar with the tasks and the notes are part of
 * syncAll(), in that order. `settings` must outlive it or be destroyed with the engine first; the
 * object never uses it after that.
 */
class AppController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(AppController)
    QML_SINGLETON

    Q_PROPERTY(ConnectionState connectionState READ connectionState NOTIFY
                   connectionStateChanged FINAL)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectionStateChanged FINAL)
    /// Between choosing a phone and the end of the handshake, PIN included.
    Q_PROPERTY(bool connecting READ isConnecting NOTIFY connectionStateChanged FINAL)
    /// "Wi-Fi", "USB" or "Bluetooth" while connected, otherwise empty.
    Q_PROPERTY(QString transportLabel READ transportLabel NOTIFY connectionStateChanged FINAL)
    /// This computer waits for the phone to connect to it.
    Q_PROPERTY(bool listening READ isListening NOTIFY listeningChanged FINAL)
    /// Device id of the phone whose data is shown; empty when none is chosen.
    Q_PROPERTY(QString currentDevice READ currentDevice NOTIFY currentDeviceChanged FINAL)
    Q_PROPERTY(QString currentDeviceName READ currentDeviceName NOTIFY currentDeviceChanged FINAL)
    /// Date and time of the last connection to the current phone, formatted.
    Q_PROPERTY(QString lastConnectionText READ lastConnectionText NOTIFY currentDeviceChanged FINAL)
    Q_PROPERTY(qpm::ui::ProfileListModel *devices READ devices CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::DeviceStatusModel *status READ status CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::FoundPhoneModel *wifiPhones READ wifiPhones CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::FoundPhoneModel *usbPhones READ usbPhones CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::FoundPhoneModel *bluetoothPhones READ bluetoothPhones CONSTANT FINAL)
    /// A USB or Bluetooth search runs; the network search runs until stopSearch().
    Q_PROPERTY(bool searching READ isSearching NOTIFY searchingChanged FINAL)
    /// Why a list of found phones is empty or incomplete; empty when nothing is wrong.
    Q_PROPERTY(QString wifiHint READ wifiHint NOTIFY hintsChanged FINAL)
    Q_PROPERTY(QString usbHint READ usbHint NOTIFY hintsChanged FINAL)
    Q_PROPERTY(QString bluetoothHint READ bluetoothHint NOTIFY hintsChanged FINAL)
    Q_PROPERTY(qpm::ui::QrCodeProvider *qrCode READ qrCode CONSTANT FINAL)
    /// The folder that holds the data of the phones.
    Q_PROPERTY(QString profilesDirectory READ profilesDirectory NOTIFY profilesDirectoryChanged FINAL)
    Q_PROPERTY(bool syncing READ isSyncing NOTIFY syncingChanged FINAL)
    /// A PIN can be kept in the keyring of the system.
    Q_PROPERTY(bool canRememberPin READ canRememberPin CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::ContactsArea *contacts READ contacts CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::CallsArea *calls READ calls CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::MessagesArea *messages READ messages CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::CalendarArea *calendar READ calendar CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::TasksArea *tasks READ tasks CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::NotesArea *notes READ notes CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::FileBrowserModel *files READ files CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::TransferQueue *transfers READ transfers CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::AppListModel *apps READ apps CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::ScreenMirror *screen READ screen CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::NotificationListModel *notifications READ notifications CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::CallControl *call READ call CONSTANT FINAL)
    Q_PROPERTY(qpm::ui::BackupController *backup READ backup CONSTANT FINAL)

public:
    enum class ConnectionState {
        Disconnected,
        /// Waiting for the phone to connect to this computer.
        Waiting,
        Connecting,
        WaitingForPin,
        Connected,
        Lost,
    };
    Q_ENUM(ConnectionState)

    enum class ConnectionKind { None, Wifi, Usb, Bluetooth };
    Q_ENUM(ConnectionKind)

    /// What a sync of one area changed on this computer.
    struct SyncChanges
    {
        int added = 0;
        int changed = 0;
        int removed = 0;
    };
    using SyncRunner = std::function<QFuture<core::Result<SyncChanges>>()>;

    /// Ports and addresses that tests replace.
    struct Environment
    {
        /// Where this computer waits for the phone.
        quint16 listenPort = 5210;
        /// Null: the address in the QR code, or every IPv4 address when the
        /// setting asks for listening.
        QHostAddress listenAddress;
        bool searchNetwork = true;
        bool searchBluetooth = true;
    };

    /**
     * `secrets` may be null; then no PIN is remembered. The data folder comes
     * from `settings`.
     */
    AppController(Settings *settings, std::unique_ptr<SecretStore> secrets,
                  const Environment &environment, QObject *parent = nullptr);
    ~AppController() override;

    /// Factory for the QML engine; uses the engine's Settings and the session keyring.
    static AppController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    [[nodiscard]] ConnectionState connectionState() const;
    [[nodiscard]] bool isConnected() const;
    [[nodiscard]] bool isConnecting() const;
    [[nodiscard]] QString transportLabel() const;
    [[nodiscard]] bool isListening() const;
    [[nodiscard]] QString currentDevice() const { return m_currentDevice; }
    [[nodiscard]] QString currentDeviceName() const;
    [[nodiscard]] QString lastConnectionText() const;
    [[nodiscard]] ProfileListModel *devices() const { return m_devices; }
    [[nodiscard]] DeviceStatusModel *status() const { return m_status; }
    [[nodiscard]] FoundPhoneModel *wifiPhones() const { return m_wifiPhones; }
    [[nodiscard]] FoundPhoneModel *usbPhones() const { return m_usbPhones; }
    [[nodiscard]] FoundPhoneModel *bluetoothPhones() const { return m_bluetoothPhones; }
    [[nodiscard]] bool isSearching() const { return m_pendingSearches > 0; }
    [[nodiscard]] QString wifiHint() const { return m_wifiHint; }
    [[nodiscard]] QString usbHint() const { return m_usbHint; }
    [[nodiscard]] QString bluetoothHint() const { return m_bluetoothHint; }
    [[nodiscard]] QrCodeProvider *qrCode() const { return m_qrCode; }
    [[nodiscard]] QString profilesDirectory() const { return m_profilesDirectory; }
    [[nodiscard]] bool isSyncing() const { return m_syncing; }
    [[nodiscard]] bool canRememberPin() const { return m_secrets != nullptr; }
    [[nodiscard]] ContactsArea *contacts() const { return m_contacts; }
    [[nodiscard]] CallsArea *calls() const { return m_calls; }
    [[nodiscard]] MessagesArea *messages() const { return m_messages; }
    [[nodiscard]] CalendarArea *calendar() const { return m_calendar; }
    [[nodiscard]] TasksArea *tasks() const { return m_tasks; }
    [[nodiscard]] NotesArea *notes() const { return m_notes; }
    [[nodiscard]] FileBrowserModel *files() const { return m_files; }
    [[nodiscard]] TransferQueue *transfers() const { return m_transfers; }
    [[nodiscard]] AppListModel *apps() const { return m_apps; }
    [[nodiscard]] ScreenMirror *screen() const { return m_screen; }
    [[nodiscard]] NotificationListModel *notifications() const { return m_notifications; }
    [[nodiscard]] CallControl *call() const { return m_call; }
    [[nodiscard]] BackupController *backup() const { return m_backup; }
    [[nodiscard]] core::service::ScreenMirrorClient *screenClient() const { return m_screenClient; }
    [[nodiscard]] core::service::NotificationService *notificationService() const
    {
        return m_notificationService;
    }
    [[nodiscard]] core::service::ClipboardService *clipboardService() const
    {
        return m_clipboardService;
    }
    [[nodiscard]] core::service::CallMonitor *callMonitor() const { return m_callMonitor; }
    [[nodiscard]] core::service::AppsService *appsService() const { return m_appsService; }
    [[nodiscard]] core::service::FileService *fileService() const { return m_fileService; }
    [[nodiscard]] core::service::ThumbnailChannel *thumbnailChannel() const
    {
        return m_thumbnailChannel;
    }
    /// The pictures behind "image://qpm/", for the image provider of the engine.
    [[nodiscard]] std::shared_ptr<PhotoSource> photoSource() const { return m_photos; }

    /// The connection, for the areas that later talk to the phone.
    [[nodiscard]] core::protocol::PhoneConnection *connection() const { return m_connection; }
    /// The store of the current phone, or null when none is chosen.
    [[nodiscard]] core::store::ProfileStore *profileStore() const { return m_store.get(); }

    /**
     * Loads the profiles, shows the phone of the last session and connects to
     * it when the settings ask for that. Waits for the phone when the
     * settings ask for listening.
     */
    Q_INVOKABLE void start();

    /**
     * Connects over Wi-Fi to "<IP address>" or "<IP address>:<port>". Returns
     * false and reports an error for anything else.
     */
    Q_INVOKABLE bool connectAddress(const QString &address);
    /// Connects over USB to the adb device `serial`; false when adb is missing.
    Q_INVOKABLE bool connectUsb(const QString &serial);
    /// Connects over Bluetooth to the paired phone at `address` ("AA:BB:...").
    Q_INVOKABLE bool connectBluetooth(const QString &address);
    /**
     * Shows the stored phone `deviceId` and connects to it in the order of the
     * settings, over each way that worked before. Returns false when there is
     * no such way.
     */
    Q_INVOKABLE bool connectDevice(const QString &deviceId);
    /// Ends the connection on request; no reconnect follows.
    Q_INVOKABLE void disconnectPhone();

    /**
     * Answers pinRequested(). With `remember`, the PIN goes to the keyring
     * once the phone has accepted it.
     */
    Q_INVOKABLE void submitPin(const QString &pin, bool remember);
    /// Gives up the connection that waits for a PIN.
    Q_INVOKABLE void cancelPin();

    /// Looks for phones in the network, at USB and over Bluetooth.
    Q_INVOKABLE void startSearch();
    Q_INVOKABLE void stopSearch();

    /// Shows the data of the stored phone `deviceId`; ends a connection to another phone.
    Q_INVOKABLE void selectDevice(const QString &deviceId);
    /// Gives the stored phone a name of 1 to 64 characters.
    Q_INVOKABLE bool renameDevice(const QString &deviceId, const QString &name);
    /// Deletes the stored data and the remembered PIN of the phone from this computer.
    Q_INVOKABLE bool removeDevice(const QString &deviceId);

    /// Syncs every registered area one after another; syncFinished() reports the changes.
    Q_INVOKABLE void syncAll();
    /// Adds an area to syncAll(); `title` is the translated name shown in the summary.
    void registerSyncArea(const QString &title, SyncRunner runner);

    /**
     * Shows new notifications of the phone on the desktop through `notifier`
     * while the settings ask for that; null shows none. Takes ownership.
     */
    void setDesktopNotifier(std::unique_ptr<DesktopNotifier> notifier);

    /// Asks the window to start a new message, as the icon in the notification area does.
    void requestNewMessage();

    /// Absolute paths of adb found in the usual places, best first.
    [[nodiscard]] Q_INVOKABLE QStringList adbSuggestions() const;

    /**
     * Whether connecting over USB would find adb with `path` as the setting:
     * an absolute path to an executable file, or empty while adbSuggestions()
     * finds one.
     */
    [[nodiscard]] Q_INVOKABLE bool adbUsable(const QString &path) const;

    /**
     * The translated text for a failure while connecting over `kind`: what
     * went wrong and what to do. Empty for a cancelled operation.
     */
    [[nodiscard]] static QString messageFor(const core::Error &error, ConnectionKind kind);

Q_SIGNALS:
    void connectionStateChanged();
    void listeningChanged();
    void currentDeviceChanged();
    void searchingChanged();
    void hintsChanged();
    void profilesDirectoryChanged();
    void syncingChanged();
    /**
     * The phone needs its PIN. `wrongPin` is true when the last PIN was
     * wrong; `remainingTries` 0 means the phone stops on the next wrong one.
     */
    void pinRequested(int remainingTries, bool wrongPin);
    void errorOccurred(const QString &message);
    /// One line per area with what changed; one line saying so when nothing can be synced.
    void syncFinished(const QString &summary);
    /// The window should come to the front and start a new message.
    void newMessageRequested();
    /// The window should come to the front and show the notifications of the phone.
    void notificationsRequested();

private:
    struct Attempt
    {
        ConnectionKind kind = ConnectionKind::None;
        QString address;
    };

    void startAttempt(const Attempt &attempt);
    void attemptFailed(const core::Error &error);
    void onStateChanged();
    void onPinRequired(int remainingTries);
    void onIdentified();
    void onFailed(const core::Error &error);
    void updateListening();
    void reloadProfiles();
    [[nodiscard]] bool openStore(const QString &deviceId);
    void closeStore();
    void setCurrentDevice(const QString &deviceId);
    void rememberPin(const QString &deviceId);
    void runNextSyncArea(int index, QStringList lines);
    void finishSearch();
    [[nodiscard]] QString resolveAdbPath() const;
    void applyConnectionOptions(std::chrono::milliseconds reconnectDelay);
    [[nodiscard]] QString defaultDisplayName() const;

    QPointer<Settings> m_settings;
    std::unique_ptr<SecretStore> m_secrets;
    Environment m_environment;
    core::protocol::PhoneConnection *m_connection = nullptr;
    core::service::DeviceStatusService *m_statusService = nullptr;
    DeviceStatusModel *m_status = nullptr;
    ProfileListModel *m_devices = nullptr;
    FoundPhoneModel *m_wifiPhones = nullptr;
    FoundPhoneModel *m_usbPhones = nullptr;
    FoundPhoneModel *m_bluetoothPhones = nullptr;
    QrCodeProvider *m_qrCode = nullptr;
    std::shared_ptr<PhotoSource> m_photos;
    ContactsArea *m_contacts = nullptr;
    CallsArea *m_calls = nullptr;
    MessagesArea *m_messages = nullptr;
    CalendarArea *m_calendar = nullptr;
    TasksArea *m_tasks = nullptr;
    NotesArea *m_notes = nullptr;
    core::service::FileService *m_fileService = nullptr;
    core::service::ThumbnailChannel *m_thumbnailChannel = nullptr;
    TransferQueue *m_transfers = nullptr;
    FileBrowserModel *m_files = nullptr;
    core::service::AppsService *m_appsService = nullptr;
    AppListModel *m_apps = nullptr;
    core::service::ScreenMirrorClient *m_screenClient = nullptr;
    ScreenMirror *m_screen = nullptr;
    core::service::NotificationService *m_notificationService = nullptr;
    core::service::ClipboardService *m_clipboardService = nullptr;
    core::service::CallMonitor *m_callMonitor = nullptr;
    NotificationListModel *m_notifications = nullptr;
    CallControl *m_call = nullptr;
    BackupController *m_backup = nullptr;
    DesktopIntegration *m_desktop = nullptr;
    std::unique_ptr<DesktopNotifier> m_notifier;
    /// The contacts the call monitor looks names up in; it keeps only a weak reference.
    std::shared_ptr<core::store::ContactStore> m_callerNames;
    core::protocol::Discovery *m_discovery = nullptr;
    BluetoothDeviceFinder *m_bluetoothFinder = nullptr;
    std::unique_ptr<core::protocol::AdbBridge> m_listAdb;
    std::unique_ptr<core::store::ProfileStore> m_store;
    QString m_profilesDirectory;
    QString m_currentDevice;
    QList<Attempt> m_attempts;
    Attempt m_attempt;
    QString m_wifiHint;
    QString m_usbHint;
    QString m_bluetoothHint;
    QString m_pinToRemember;
    QList<std::pair<QString, SyncRunner>> m_syncAreas;
    std::chrono::milliseconds m_reconnectDelay{0};
    quint64 m_pinLookup = 0;
    int m_pendingSearches = 0;
    bool m_reachedReady = false;
    bool m_reconnecting = false;
    bool m_waitingForPin = false;
    bool m_pinSubmitted = false;
    bool m_storedPinTried = false;
    bool m_listening = false;
    /// A call of this object is starting a connection right now.
    bool m_startingAttempt = false;
    /// The current connection was opened by the phone, after the QR code.
    bool m_inbound = false;
    bool m_syncing = false;
};

} // namespace qpm::ui
