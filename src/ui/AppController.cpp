#include "ui/AppController.h"

#include "bluetooth/BluetoothDeviceFinder.h"
#include "bluetooth/RfcommTransport.h"
#include "core/Logging.h"
#include "core/protocol/AdbBridge.h"
#include "core/protocol/Discovery.h"
#include "core/protocol/PhoneConnection.h"
#include "core/service/DeviceStatusService.h"
#include "core/service/FileService.h"
#include "core/service/ThumbnailChannel.h"
#include "platform/SecretStore.h"
#include "ui/DeviceStatusModel.h"
#include "ui/FoundPhoneModel.h"
#include "ui/ProfileListModel.h"
#include "ui/QrCodeProvider.h"
#include "ui/Settings.h"
#include "ui/contacts/PhoneImageProvider.h"

#include <QBluetoothAddress>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QQmlEngine>
#include <QScopeGuard>
#include <QUrl>

#include <chrono>

namespace qpm::ui {

namespace {

using core::Error;
using core::ErrorCode;
using core::Result;
using core::protocol::PhoneConnection;
using core::store::DeviceProfile;
using core::store::ProfileStore;
using core::store::TransportKind;

constexpr quint16 phonePort = 5210;
constexpr std::chrono::seconds reconnectDelay{5};
constexpr qsizetype maxDisplayName = 64;

struct HostAndPort
{
    QString host;
    quint16 port = phonePort;
};

// "<IP address>" or "<IP address>:<port>"; IPv6 in brackets when it carries
// a port.
std::optional<HostAndPort> parseAddress(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return std::nullopt;
    const QHostAddress plain(trimmed);
    if (!plain.isNull())
        return HostAndPort{plain.toString(), phonePort};
    const QUrl url(QStringLiteral("tcp://") + trimmed);
    if (!url.isValid() || url.port() <= 0 || !url.userInfo().isEmpty() || !url.path().isEmpty())
        return std::nullopt;
    const QHostAddress host(url.host());
    if (host.isNull())
        return std::nullopt;
    return HostAndPort{host.toString(), quint16(url.port())};
}

TransportKind transportKind(AppController::ConnectionKind kind)
{
    switch (kind) {
    case AppController::ConnectionKind::Wifi:
        return TransportKind::Tcp;
    case AppController::ConnectionKind::Usb:
        return TransportKind::Adb;
    case AppController::ConnectionKind::Bluetooth:
        return TransportKind::Bluetooth;
    case AppController::ConnectionKind::None:
        break;
    }
    return TransportKind::None;
}

bool isExecutableFile(const QString &path)
{
    const QFileInfo info(path);
    return info.isAbsolute() && info.isFile() && info.isExecutable();
}

} // namespace

AppController::AppController(Settings *settings, std::unique_ptr<SecretStore> secrets,
                             const Environment &environment, QObject *parent)
    : QObject(parent),
      m_settings(settings),
      m_secrets(std::move(secrets)),
      m_environment(environment),
      m_connection(new PhoneConnection(this)),
      m_statusService(new core::service::DeviceStatusService(m_connection, this)),
      m_status(new DeviceStatusModel(m_statusService, this)),
      m_devices(new ProfileListModel(this)),
      m_wifiPhones(new FoundPhoneModel(this)),
      m_usbPhones(new FoundPhoneModel(this)),
      m_bluetoothPhones(new FoundPhoneModel(this)),
      m_qrCode(new QrCodeProvider(this)),
      m_photos(std::make_shared<PhotoSource>()),
      m_contacts(new ContactsArea(m_connection, m_photos, this)),
      m_calls(new CallsArea(m_connection, m_contacts->service(), this)),
      m_messages(new MessagesArea(m_connection, m_statusService, this)),
      m_calendar(new CalendarArea(m_connection, this)),
      m_tasks(new TasksArea(m_calendar->service(), this)),
      m_notes(new NotesArea(m_connection, this)),
      m_fileService(new core::service::FileService(m_connection, this)),
      m_thumbnailChannel(new core::service::ThumbnailChannel(m_connection, this)),
      m_transfers(new TransferQueue(m_fileService, this)),
      m_files(new FileBrowserModel(m_fileService, m_thumbnailChannel, m_transfers, m_photos, this))
{
    Q_ASSERT(settings);
    const QString configured = settings->dataDirectory();
    m_profilesDirectory =
        configured.isEmpty() ? ProfileStore::defaultProfilesDirectory() : configured;

    connect(m_connection, &PhoneConnection::stateChanged, this, &AppController::onStateChanged);
    connect(m_connection, &PhoneConnection::pinRequired, this, &AppController::onPinRequired);
    connect(m_connection, &PhoneConnection::deviceIdentified, this, &AppController::onIdentified);
    connect(m_connection, &PhoneConnection::failed, this, &AppController::onFailed);
    connect(m_qrCode, &QrCodeProvider::activeChanged, this, &AppController::updateListening);
    registerSyncArea(tr("Contacts"), [this]() {
        return m_contacts->sync().then(
            this, [](const Result<core::store::ContactChanges> &pulled) -> Result<SyncChanges> {
                if (!pulled.hasValue())
                    return pulled.error();
                return SyncChanges{pulled.value().added, pulled.value().changed,
                                   pulled.value().removed};
            });
    });
    registerSyncArea(tr("Calls"), [this]() {
        return m_calls->sync().then(
            this, [](const Result<core::store::CallChanges> &pulled) -> Result<SyncChanges> {
                if (!pulled.hasValue())
                    return pulled.error();
                return SyncChanges{pulled.value().added, pulled.value().changed,
                                   pulled.value().removed};
            });
    });
    registerSyncArea(tr("Messages"), [this]() {
        return m_messages->sync().then(
            this, [](const Result<core::store::MessageChanges> &pulled) -> Result<SyncChanges> {
                if (!pulled.hasValue())
                    return pulled.error();
                return SyncChanges{pulled.value().added, pulled.value().changed,
                                   pulled.value().removed};
            });
    });
    registerSyncArea(tr("Calendar and tasks"), [this]() {
        return m_calendar->sync().then(
            this, [](const Result<core::store::OrganizerChanges> &pulled) -> Result<SyncChanges> {
                if (!pulled.hasValue())
                    return pulled.error();
                return SyncChanges{pulled.value().added, pulled.value().changed,
                                   pulled.value().removed};
            });
    });
    registerSyncArea(tr("Notes"), [this]() {
        return m_notes->sync().then(
            this, [](const Result<core::store::OrganizerChanges> &pulled) -> Result<SyncChanges> {
                if (!pulled.hasValue())
                    return pulled.error();
                return SyncChanges{pulled.value().added, pulled.value().changed,
                                   pulled.value().removed};
            });
    });
    m_calendar->setHolidayRegion(settings->holidayRegion());
    connect(settings, &Settings::holidayRegionChanged, this, [this]() {
        if (m_settings)
            m_calendar->setHolidayRegion(m_settings->holidayRegion());
    });
    connect(m_contacts->service(), &core::service::ContactsService::contactsChanged, m_calls,
            &CallsArea::refreshNames);
    connect(m_contacts->service(), &core::service::ContactsService::contactsChanged, m_messages,
            &MessagesArea::refreshNames);
    connect(settings, &Settings::listenForPhoneChanged, this, &AppController::updateListening);
    connect(settings, &Settings::dataDirectoryChanged, this, [this]() {
        const QString configuredNow = m_settings->dataDirectory();
        const QString directory =
            configuredNow.isEmpty() ? ProfileStore::defaultProfilesDirectory() : configuredNow;
        if (directory == m_profilesDirectory)
            return;
        m_profilesDirectory = directory;
        closeStore();
        if (!m_currentDevice.isEmpty() && (isConnected() || isConnecting()))
            static_cast<void>(openStore(m_currentDevice));
        else
            setCurrentDevice(QString());
        Q_EMIT profilesDirectoryChanged();
        reloadProfiles();
    });
}

AppController::~AppController()
{
    // The connection is a child and goes after this destructor; nothing it
    // reports may reach the half-destroyed controller.
    m_connection->disconnect(this);
}

AppController *AppController::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(jsEngine)
    auto *settings = qmlEngine->singletonInstance<Settings *>("QPhoneManager.Ui", "Settings");
    auto *controller = new AppController(settings, std::make_unique<SecretStore>(), Environment());
    // The engine takes ownership of the provider.
    qmlEngine->addImageProvider(QStringLiteral("qpm"),
                                new PhoneImageProvider(controller->photoSource()));
    return controller;
}

AppController::ConnectionState AppController::connectionState() const
{
    switch (m_connection->state()) {
    case PhoneConnection::State::Disconnected:
        return ConnectionState::Disconnected;
    case PhoneConnection::State::Discovering:
        return ConnectionState::Waiting;
    case PhoneConnection::State::Connecting:
    case PhoneConnection::State::Authenticating:
    case PhoneConnection::State::Handshaking:
        return m_waitingForPin ? ConnectionState::WaitingForPin : ConnectionState::Connecting;
    case PhoneConnection::State::Ready:
    case PhoneConnection::State::Busy:
        return ConnectionState::Connected;
    case PhoneConnection::State::Lost:
        break;
    }
    return ConnectionState::Lost;
}

bool AppController::isConnected() const
{
    return connectionState() == ConnectionState::Connected;
}

bool AppController::isConnecting() const
{
    const ConnectionState state = connectionState();
    return state == ConnectionState::Connecting || state == ConnectionState::WaitingForPin;
}

QString AppController::transportLabel() const
{
    if (!isConnected())
        return {};
    switch (m_attempt.kind) {
    case ConnectionKind::Wifi:
        return tr("Wi-Fi");
    case ConnectionKind::Usb:
        return tr("USB");
    case ConnectionKind::Bluetooth:
        return tr("Bluetooth");
    case ConnectionKind::None:
        break;
    }
    return {};
}

bool AppController::isListening() const
{
    return m_listening;
}

QString AppController::currentDeviceName() const
{
    const int row = m_devices->indexOf(m_currentDevice);
    return row < 0 ? QString() : m_devices->profiles().at(row).displayName;
}

QString AppController::lastConnectionText() const
{
    const int row = m_devices->indexOf(m_currentDevice);
    if (row < 0)
        return {};
    const QDateTime last = m_devices->profiles().at(row).lastConnection;
    return last.isValid() ? QLocale().toString(last.toLocalTime(), QLocale::ShortFormat) : QString();
}

void AppController::start()
{
    reloadProfiles();
    if (m_settings) {
        const QString last = m_settings->lastDevice();
        if (!last.isEmpty() && m_devices->indexOf(last) >= 0) {
            selectDevice(last);
            if (m_settings->autoConnect())
                static_cast<void>(connectDevice(last));
        }
    }
    updateListening();
}

bool AppController::connectAddress(const QString &address)
{
    if (!parseAddress(address)) {
        Q_EMIT errorOccurred(
            tr("Enter the IP address of the phone, for example 192.168.1.20. MyPhoneExplorer "
               "Client shows it on the phone."));
        return false;
    }
    m_attempts.clear();
    startAttempt({ConnectionKind::Wifi, address.trimmed()});
    return true;
}

bool AppController::connectUsb(const QString &serial)
{
    if (resolveAdbPath().isEmpty()) {
        Q_EMIT errorOccurred(messageFor(Error(ErrorCode::InvalidInput, {}), ConnectionKind::Usb));
        return false;
    }
    m_attempts.clear();
    startAttempt({ConnectionKind::Usb, serial});
    return true;
}

bool AppController::connectBluetooth(const QString &address)
{
    if (QBluetoothAddress(address).isNull()) {
        Q_EMIT errorOccurred(tr("Choose a paired phone from the list."));
        return false;
    }
    m_attempts.clear();
    startAttempt({ConnectionKind::Bluetooth, address});
    return true;
}

bool AppController::connectDevice(const QString &deviceId)
{
    const int row = m_devices->indexOf(deviceId);
    if (row < 0)
        return false;
    const DeviceProfile profile = m_devices->profiles().at(row);
    selectDevice(deviceId);

    QList<Attempt> attempts;
    const QStringList order = m_settings ? m_settings->connectionOrder() : QStringList();
    for (const QString &kind : order) {
        if (kind == QLatin1StringView("usb") && !profile.adbSerial.isEmpty() &&
            !resolveAdbPath().isEmpty()) {
            attempts.append({ConnectionKind::Usb, profile.adbSerial});
        } else if (kind == QLatin1StringView("wifi") && !profile.fixedAddress.isEmpty()) {
            attempts.append({ConnectionKind::Wifi, profile.fixedAddress});
        } else if (kind == QLatin1StringView("bluetooth") && !profile.btAddress.isEmpty()) {
            attempts.append({ConnectionKind::Bluetooth, profile.btAddress});
        }
    }
    if (attempts.isEmpty()) {
        Q_EMIT errorOccurred(
            tr("This phone was not connected in a way that can be repeated. Choose Connect "
               "and pick how to connect."));
        return false;
    }
    m_attempts = attempts;
    startAttempt(m_attempts.takeFirst());
    return true;
}

void AppController::disconnectPhone()
{
    m_attempts.clear();
    m_reconnecting = false;
    m_reachedReady = false;
    m_waitingForPin = false;
    m_pinToRemember.clear();
    m_connection->disconnectFromPhone();
    Q_EMIT connectionStateChanged();
}

void AppController::startAttempt(const Attempt &attempt)
{
    m_attempt = attempt;
    m_inbound = false;
    m_reachedReady = false;
    m_reconnecting = false;
    PhoneConnection::Options options;
    options.reconnectDelay = std::chrono::milliseconds(0);
    m_connection->setOptions(options);
    m_startingAttempt = true;
    const auto done = qScopeGuard([this]() { m_startingAttempt = false; });

    switch (attempt.kind) {
    case ConnectionKind::Wifi: {
        const std::optional<HostAndPort> target = parseAddress(attempt.address);
        if (!target) {
            attemptFailed(Error(ErrorCode::InvalidInput, QStringLiteral("Invalid address")));
            return;
        }
        m_connection->connectTcp(target->host, target->port);
        return;
    }
    case ConnectionKind::Usb: {
        const QString adb = resolveAdbPath();
        if (adb.isEmpty()) {
            attemptFailed(Error(ErrorCode::InvalidInput, QStringLiteral("adb not found")));
            return;
        }
        m_connection->connectAdb(adb, attempt.address);
        return;
    }
    case ConnectionKind::Bluetooth:
        m_connection->connectTransport(
            std::make_unique<RfcommTransport>(QBluetoothAddress(attempt.address)));
        return;
    case ConnectionKind::None:
        break;
    }
}

// An attempt that could not even start counts like one the phone refused.
void AppController::attemptFailed(const Error &error)
{
    if (!m_attempts.isEmpty()) {
        startAttempt(m_attempts.takeFirst());
        return;
    }
    const QString message = messageFor(error, m_attempt.kind);
    if (!message.isEmpty())
        Q_EMIT errorOccurred(message);
}

void AppController::onStateChanged()
{
    if (m_connection->state() == PhoneConnection::State::Connecting) {
        // A connection that no call of this object started is the phone
        // connecting to this computer, not a retry of the last attempt.
        if (!m_startingAttempt && !m_reconnecting) {
            m_attempt = {};
            m_attempts.clear();
            m_inbound = true;
        }
        m_storedPinTried = false;
        m_pinSubmitted = false;
        m_waitingForPin = false;
        ++m_pinLookup;
    }
    if (m_connection->state() == PhoneConnection::State::Disconnected ||
        m_connection->state() == PhoneConnection::State::Lost) {
        m_waitingForPin = false;
    }
    m_contacts->setConnected(isConnected());
    m_calls->setConnected(isConnected());
    m_messages->setConnected(isConnected());
    m_calendar->setConnected(isConnected());
    m_tasks->setConnected(isConnected());
    m_notes->setConnected(isConnected());
    m_transfers->setConnected(isConnected());
    m_files->setConnected(isConnected());
    Q_EMIT connectionStateChanged();
}

void AppController::onPinRequired(int remainingTries)
{
    const bool wrong = std::exchange(m_pinSubmitted, false);
    m_waitingForPin = true;
    Q_EMIT connectionStateChanged();

    const QString deviceId = m_connection->identity().deviceId;
    if (!wrong && !m_storedPinTried && m_secrets && !deviceId.isEmpty()) {
        m_storedPinTried = true;
        const quint64 lookup = ++m_pinLookup;
        m_secrets->pin(deviceId).then(
            this, [this, lookup, remainingTries](const Result<std::optional<QString>> &stored) {
                if (lookup != m_pinLookup || !m_waitingForPin)
                    return;
                if (stored.hasValue() && stored.value()) {
                    m_pinSubmitted = true;
                    m_waitingForPin = false;
                    Q_EMIT connectionStateChanged();
                    m_connection->submitPin(*stored.value());
                    return;
                }
                if (!stored.hasValue())
                    qCInfo(core::lcUi) << "No PIN from the keyring:" << stored.error().message();
                Q_EMIT pinRequested(remainingTries, false);
            });
        return;
    }
    Q_EMIT pinRequested(remainingTries, wrong);
}

void AppController::submitPin(const QString &pin, bool remember)
{
    if (!m_waitingForPin || pin.isEmpty())
        return;
    m_pinSubmitted = true;
    m_waitingForPin = false;
    m_pinToRemember = remember && m_secrets ? pin : QString();
    Q_EMIT connectionStateChanged();
    m_connection->submitPin(pin);
}

void AppController::cancelPin()
{
    disconnectPhone();
}

void AppController::onIdentified()
{
    const core::protocol::DeviceIdentity &identity = m_connection->identity();
    m_attempts.clear();
    m_reachedReady = true;
    m_reconnecting = false;
    m_waitingForPin = false;

    PhoneConnection::Options options;
    const bool autoConnect = m_settings && m_settings->autoConnect();
    options.reconnectDelay = autoConnect ? std::chrono::milliseconds(reconnectDelay)
                                         : std::chrono::milliseconds(0);
    m_connection->setOptions(options);

    if (m_attempt.kind == ConnectionKind::None) {
        // The phone connected to this computer after scanning the QR code.
        m_attempt.kind = ConnectionKind::Wifi;
        const std::optional<core::protocol::Endpoint> peer =
            m_connection->sideChannelEndpoint(phonePort);
        m_attempt.address = peer ? peer->host : QString();
    }

    if (openStore(identity.deviceId)) {
        Result<DeviceProfile> loaded = m_store->loadProfile();
        DeviceProfile profile = loaded.hasValue() ? loaded.value() : DeviceProfile();
        profile.deviceId = identity.deviceId;
        if (profile.displayName.isEmpty())
            profile.displayName = defaultDisplayName();
        profile.model = identity.model;
        profile.manufacturer = identity.manufacturer;
        profile.androidApi = identity.androidApi;
        profile.clientVersion = identity.clientVersion;
        profile.lastConnection = QDateTime::currentDateTimeUtc();
        profile.transport = transportKind(m_attempt.kind);
        if (m_attempt.kind == ConnectionKind::Wifi && !m_attempt.address.isEmpty())
            profile.fixedAddress = m_attempt.address;
        else if (m_attempt.kind == ConnectionKind::Usb)
            profile.adbSerial = m_attempt.address;
        else if (m_attempt.kind == ConnectionKind::Bluetooth)
            profile.btAddress = m_attempt.address;
        if (!m_pinToRemember.isEmpty())
            profile.pinStored = true;
        const Result<void> saved = m_store->saveProfile(profile);
        if (!saved.hasValue()) {
            qCWarning(core::lcUi) << "Profile not saved:" << saved.error().message();
            Q_EMIT errorOccurred(tr("The data of this phone cannot be saved on this computer. "
                                    "Check the storage folder in the settings."));
        }
        reloadProfiles();
        setCurrentDevice(identity.deviceId);
        if (m_settings)
            m_settings->setLastDevice(identity.deviceId);
    }
    rememberPin(identity.deviceId);
    Q_EMIT connectionStateChanged();
}

void AppController::rememberPin(const QString &deviceId)
{
    const QString pin = std::exchange(m_pinToRemember, QString());
    if (pin.isEmpty() || !m_secrets)
        return;
    m_secrets->storePin(deviceId, pin).then(this, [this](const Result<void> &stored) {
        if (stored.hasValue())
            return;
        qCWarning(core::lcUi) << "PIN not stored:" << stored.error().message();
        Q_EMIT errorOccurred(tr("The PIN could not be saved, because the keyring of the "
                                "system is not available. You will be asked for it next time."));
    });
}

void AppController::onFailed(const Error &error)
{
    if (!m_reachedReady && !m_reconnecting) {
        attemptFailed(error);
        return;
    }
    if (m_reconnecting)
        return;
    m_reachedReady = false;
    // The connection itself reconnects only to a phone it reached on its
    // own over Wi-Fi or USB, not to one that connected to this computer.
    const bool willReconnect = m_settings && m_settings->autoConnect() && !m_inbound &&
                               (m_attempt.kind == ConnectionKind::Wifi ||
                                m_attempt.kind == ConnectionKind::Usb);
    if (willReconnect) {
        m_reconnecting = true;
        Q_EMIT errorOccurred(tr("The connection to the phone was lost. QPhoneManager connects "
                                "again as soon as the phone can be reached."));
        return;
    }
    const QString message = messageFor(error, m_attempt.kind);
    if (!message.isEmpty())
        Q_EMIT errorOccurred(message);
}

void AppController::updateListening()
{
    const bool want = m_qrCode->isActive() || (m_settings && m_settings->listenForPhone());
    if (!want) {
        if (m_connection->isListening())
            m_connection->stopListening();
    } else {
        QHostAddress address = m_environment.listenAddress;
        if (address.isNull()) {
            address = m_qrCode->isActive() && !m_qrCode->ownHostAddress().isNull()
                          ? m_qrCode->ownHostAddress()
                          : QHostAddress(QHostAddress::AnyIPv4);
        }
        const Result<void> listening =
            m_connection->isListening()
                ? Result<void>()
                : m_connection->listenForPhone(m_environment.listenPort, address);
        if (!listening.hasValue()) {
            qCWarning(core::lcUi) << "Cannot listen:" << listening.error().message();
            Q_EMIT errorOccurred(tr("This computer cannot wait for the phone, because port %1 "
                                    "is in use. Close the program that uses it, then try again.")
                                     .arg(m_environment.listenPort));
        }
    }
    const bool listening = m_connection->isListening();
    if (listening == m_listening)
        return;
    m_listening = listening;
    Q_EMIT listeningChanged();
}

void AppController::startSearch()
{
    stopSearch();
    m_usbPhones->clear();
    m_bluetoothPhones->clear();
    m_wifiHint.clear();
    m_usbHint.clear();
    m_bluetoothHint.clear();

    if (m_environment.searchNetwork) {
        if (!m_discovery) {
            m_discovery = new core::protocol::Discovery(this);
            connect(m_discovery, &core::protocol::Discovery::deviceFound, this,
                    [this](const core::protocol::DiscoveredDevice &device) {
                        const QString address = device.address.toString();
                        m_wifiPhones->upsert(FoundPhone{
                            device.model.isEmpty() ? address : device.model, address, address,
                            device.deviceId, true});
                    });
        }
        const Result<void> started = m_discovery->start();
        if (!started.hasValue()) {
            qCInfo(core::lcUi) << "Network search not started:" << started.error().message();
            m_wifiHint = tr("This computer cannot search the network right now. Enter the "
                            "address of the phone instead.");
        }
    }

    const QString adb = resolveAdbPath();
    if (adb.isEmpty()) {
        m_usbHint = tr("adb was not found. Install the Android platform tools or choose adb "
                       "in the settings.");
    } else {
        ++m_pendingSearches;
        m_listAdb = std::make_unique<core::protocol::AdbBridge>(adb);
        m_listAdb->devices().then(
            this, [this](const Result<QList<core::protocol::AdbDevice>> &devices) {
                if (!devices.hasValue()) {
                    qCInfo(core::lcUi) << "adb devices failed:" << devices.error().message();
                    m_usbHint = tr("adb did not work. Check the adb path in the settings.");
                } else {
                    for (const core::protocol::AdbDevice &device : devices.value()) {
                        const bool ready = device.state == QLatin1StringView("device");
                        QString label = device.model;
                        label.replace(u'_', u' ');
                        QString detail = device.serial;
                        if (device.state == QLatin1StringView("unauthorized"))
                            detail = tr("Allow USB debugging on the phone, then search again.");
                        else if (!ready)
                            detail = tr("The phone is not ready yet. Unlock it and search again.");
                        m_usbPhones->upsert(FoundPhone{label.isEmpty() ? device.serial : label,
                                                       detail, device.serial, QString(), ready});
                    }
                    if (devices.value().isEmpty()) {
                        m_usbHint = tr("No phone found at USB. Connect the cable and allow USB "
                                       "debugging on the phone.");
                    }
                }
                finishSearch();
            });
    }

    if (m_environment.searchBluetooth) {
        if (!m_bluetoothFinder) {
            m_bluetoothFinder = new BluetoothDeviceFinder(this);
            connect(m_bluetoothFinder, &BluetoothDeviceFinder::deviceFound, this,
                    [this](const PairedDevice &device) {
                        m_bluetoothPhones->upsert(FoundPhone{
                            device.name.isEmpty() ? device.address : device.name,
                            device.address, device.address, QString(), true});
                    });
            connect(m_bluetoothFinder, &BluetoothDeviceFinder::failed, this,
                    [this](const Error &error) {
                        m_bluetoothHint =
                            error.code() == ErrorCode::NotSupported
                                ? tr("This computer has no Bluetooth, or it is turned off. "
                                     "Turn Bluetooth on, or connect over Wi-Fi or USB.")
                                : tr("The Bluetooth search failed. Turn Bluetooth on and try "
                                     "again.");
                        Q_EMIT hintsChanged();
                    });
            connect(m_bluetoothFinder, &BluetoothDeviceFinder::finished, this, [this]() {
                if (m_bluetoothPhones->count() == 0 && m_bluetoothHint.isEmpty()) {
                    m_bluetoothHint = tr("No paired phone found. Pair the phone in the system "
                                         "settings first.");
                }
                finishSearch();
            });
        }
        ++m_pendingSearches;
        m_bluetoothFinder->start();
    }
    Q_EMIT hintsChanged();
    Q_EMIT searchingChanged();
}

void AppController::finishSearch()
{
    if (m_pendingSearches > 0)
        --m_pendingSearches;
    Q_EMIT hintsChanged();
    Q_EMIT searchingChanged();
}

void AppController::stopSearch()
{
    if (m_discovery)
        m_discovery->stop();
    if (m_bluetoothFinder && m_bluetoothFinder->isRunning()) {
        m_bluetoothFinder->stop();
    }
    if (m_listAdb) {
        // The running "adb devices" ends on its own; its answer no longer counts.
        m_listAdb.reset();
    }
    if (m_pendingSearches == 0)
        return;
    m_pendingSearches = 0;
    Q_EMIT searchingChanged();
}

void AppController::selectDevice(const QString &deviceId)
{
    if (deviceId == m_currentDevice)
        return;
    if (isConnected() || isConnecting())
        disconnectPhone();
    if (!openStore(deviceId))
        return;
    setCurrentDevice(deviceId);
    if (m_settings)
        m_settings->setLastDevice(deviceId);
}

bool AppController::renameDevice(const QString &deviceId, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > maxDisplayName) {
        Q_EMIT errorOccurred(tr("Enter a name with 1 to 64 characters."));
        return false;
    }
    std::unique_ptr<ProfileStore> other;
    ProfileStore *store = m_store && m_store->deviceId() == deviceId ? m_store.get() : nullptr;
    if (!store) {
        other = std::make_unique<ProfileStore>(m_profilesDirectory, deviceId);
        if (!other->open().hasValue()) {
            Q_EMIT errorOccurred(tr("The name could not be saved. Check the storage folder in "
                                    "the settings."));
            return false;
        }
        store = other.get();
    }
    Result<DeviceProfile> profile = store->loadProfile();
    if (!profile.hasValue()) {
        Q_EMIT errorOccurred(
            tr("The name could not be saved. Check the storage folder in the settings."));
        return false;
    }
    DeviceProfile changed = profile.value();
    changed.displayName = trimmed;
    if (!store->saveProfile(changed).hasValue()) {
        Q_EMIT errorOccurred(
            tr("The name could not be saved. Check the storage folder in the settings."));
        return false;
    }
    reloadProfiles();
    Q_EMIT currentDeviceChanged();
    return true;
}

bool AppController::removeDevice(const QString &deviceId)
{
    if (deviceId == m_currentDevice) {
        if (isConnected() || isConnecting())
            disconnectPhone();
        closeStore();
    }
    const Result<void> removed = ProfileStore::removeProfile(m_profilesDirectory, deviceId);
    if (!removed.hasValue()) {
        qCWarning(core::lcUi) << "Profile not removed:" << removed.error().message();
        Q_EMIT errorOccurred(tr("The phone could not be removed from this computer. Check the "
                                "storage folder in the settings, then try again."));
        reloadProfiles();
        return false;
    }
    if (m_secrets) {
        m_secrets->removePin(deviceId).then(this, [](const Result<void> &pinRemoved) {
            // Nothing is left to undo: the profile is gone either way, and a
            // PIN that stays in the keyring is only used for this phone again.
            if (!pinRemoved.hasValue())
                qCInfo(core::lcUi) << "PIN not removed:" << pinRemoved.error().message();
        });
    }
    if (deviceId == m_currentDevice) {
        setCurrentDevice(QString());
        if (m_settings)
            m_settings->setLastDevice(QString());
    }
    reloadProfiles();
    return true;
}

void AppController::registerSyncArea(const QString &title, SyncRunner runner)
{
    m_syncAreas.append({title, std::move(runner)});
}

void AppController::syncAll()
{
    if (m_syncing)
        return;
    if (!isConnected()) {
        Q_EMIT errorOccurred(tr("Connect the phone first, then sync."));
        return;
    }
    if (m_syncAreas.isEmpty()) {
        Q_EMIT syncFinished(tr("There is nothing to sync yet."));
        return;
    }
    m_syncing = true;
    Q_EMIT syncingChanged();
    runNextSyncArea(0, {});
}

void AppController::runNextSyncArea(int index, QStringList lines)
{
    if (index >= m_syncAreas.size()) {
        m_syncing = false;
        Q_EMIT syncingChanged();
        Q_EMIT syncFinished(lines.join(u'\n'));
        return;
    }
    const QString title = m_syncAreas.at(index).first;
    m_syncAreas.at(index).second().then(
        this, [this, index, title, lines](const Result<SyncChanges> &result) mutable {
            if (!result.hasValue()) {
                lines.append(tr("%1: not synced. %2")
                                 .arg(title, messageFor(result.error(), m_attempt.kind)));
            } else {
                const SyncChanges &changes = result.value();
                lines.append(tr("%1: %2 new, %3 changed, %4 deleted")
                                 .arg(title)
                                 .arg(changes.added)
                                 .arg(changes.changed)
                                 .arg(changes.removed));
            }
            runNextSyncArea(index + 1, lines);
        });
}

QStringList AppController::adbSuggestions() const
{
    QStringList candidates;
    for (const char *variable : {"ANDROID_HOME", "ANDROID_SDK_ROOT"}) {
        const QString sdk = qEnvironmentVariable(variable);
        if (QDir::isAbsolutePath(sdk))
            candidates.append(QDir(sdk).filePath(QStringLiteral("platform-tools/adb")));
    }
    candidates.append(QDir::home().filePath(QStringLiteral("Android/Sdk/platform-tools/adb")));
    candidates.append(QStringLiteral("/usr/bin/adb"));
    candidates.append(QStringLiteral("/usr/local/bin/adb"));
    candidates.append(QStringLiteral("/usr/lib/android-sdk/platform-tools/adb"));
    candidates.append(QStringLiteral("/opt/android-sdk/platform-tools/adb"));

    QStringList found;
    for (const QString &candidate : std::as_const(candidates)) {
        const QString cleaned = QDir::cleanPath(candidate);
        if (isExecutableFile(cleaned) && !found.contains(cleaned))
            found.append(cleaned);
    }
    return found;
}

QString AppController::resolveAdbPath() const
{
    const QString configured = m_settings ? m_settings->adbPath() : QString();
    if (!configured.isEmpty())
        return isExecutableFile(configured) ? configured : QString();
    return adbSuggestions().value(0);
}

QString AppController::defaultDisplayName() const
{
    const core::protocol::DeviceIdentity &identity = m_connection->identity();
    const QString model = identity.model.trimmed();
    return model.isEmpty() ? tr("Phone") : model.left(maxDisplayName);
}

void AppController::reloadProfiles()
{
    const Result<QList<DeviceProfile>> profiles = ProfileStore::listProfiles(m_profilesDirectory);
    if (!profiles.hasValue()) {
        qCWarning(core::lcUi) << "Profiles not listed:" << profiles.error().message();
        m_devices->setProfiles({}, m_currentDevice);
    } else {
        m_devices->setProfiles(profiles.value(), m_currentDevice);
    }
    Q_EMIT currentDeviceChanged();
}

bool AppController::openStore(const QString &deviceId)
{
    if (m_store && m_store->deviceId() == deviceId)
        return true;
    closeStore();
    auto store = std::make_unique<ProfileStore>(m_profilesDirectory, deviceId);
    const Result<void> opened = store->open();
    if (!opened.hasValue()) {
        qCWarning(core::lcUi) << "Profile not opened:" << opened.error().message();
        Q_EMIT errorOccurred(tr("The data of this phone cannot be opened on this computer. "
                                "Check the storage folder in the settings."));
        return false;
    }
    m_store = std::move(store);
    m_contacts->setProfileStore(m_store.get());
    m_calls->setProfileStore(m_store.get());
    m_messages->setProfileStore(m_store.get());
    m_calendar->setProfileStore(m_store.get());
    m_tasks->setStore(m_calendar->store());
    m_notes->setStore(m_calendar->store());
    return true;
}

void AppController::closeStore()
{
    // The areas hold the store; they let go of it before it closes.
    m_contacts->setProfileStore(nullptr);
    m_calls->setProfileStore(nullptr);
    m_messages->setProfileStore(nullptr);
    m_notes->setStore(nullptr);
    m_tasks->setStore(nullptr);
    m_calendar->setProfileStore(nullptr);
    m_store.reset();
}

void AppController::setCurrentDevice(const QString &deviceId)
{
    if (deviceId == m_currentDevice)
        return;
    m_currentDevice = deviceId;
    m_devices->setProfiles(m_devices->profiles(), deviceId);
    Q_EMIT currentDeviceChanged();
}

QString AppController::messageFor(const Error &error, ConnectionKind kind)
{
    switch (error.code()) {
    case ErrorCode::None:
    case ErrorCode::Cancelled:
        return {};
    case ErrorCode::Timeout:
        if (kind == ConnectionKind::Bluetooth) {
            return tr("The phone does not answer over Bluetooth. Turn Bluetooth on at the phone, "
                      "keep it close and try again.");
        }
        return tr("The phone does not answer. Check that MyPhoneExplorer Client is open on the "
                  "phone, then try again.");
    case ErrorCode::ConnectionClosed:
        if (kind == ConnectionKind::Wifi) {
            return tr("The phone cannot be reached over Wi-Fi. Check that it is in the same "
                      "network and that MyPhoneExplorer Client is open, then try again.");
        }
        return tr("The connection to the phone ended. Open MyPhoneExplorer Client on the phone, "
                  "then connect again.");
    case ErrorCode::AlreadyConnected:
        return tr("The phone is connected to another computer. End that connection first, then "
                  "try again.");
    case ErrorCode::WifiDisabled:
        return tr("MyPhoneExplorer Client does not allow Wi-Fi connections. Allow them in the "
                  "app settings on the phone, or connect over USB.");
    case ErrorCode::AccessDenied:
        return tr("The phone asks for a PIN. Connect again and enter the PIN you set in "
                  "MyPhoneExplorer Client.");
    case ErrorCode::WrongPin:
        return tr("MyPhoneExplorer Client stopped after too many wrong PINs. Open the app on "
                  "the phone again, then connect.");
    case ErrorCode::PermissionDenied:
        return tr("MyPhoneExplorer Client lacks a permission on the phone. Allow it in the "
                  "Android settings under Apps.");
    case ErrorCode::NotSupported:
        if (kind == ConnectionKind::Bluetooth) {
            return tr("This computer has no Bluetooth, or it is turned off. Turn Bluetooth on, "
                      "or connect over Wi-Fi or USB.");
        }
        return tr("The phone does not support this. Update MyPhoneExplorer Client on the "
                  "phone.");
    case ErrorCode::NotFound:
        if (kind == ConnectionKind::Usb) {
            return tr("MyPhoneExplorer Client is not installed on the phone. Install it from "
                      "Google Play, then try again.");
        }
        return tr("The phone did not find what was asked for. Sync again.");
    case ErrorCode::InvalidInput:
        if (kind == ConnectionKind::Usb) {
            return tr("adb was not found. Install the Android platform tools or choose adb in "
                      "the settings.");
        }
        return tr("Enter the IP address of the phone, for example 192.168.1.20. MyPhoneExplorer "
                  "Client shows it on the phone.");
    case ErrorCode::IoFailure:
        if (kind == ConnectionKind::Usb) {
            return tr("adb cannot reach the phone. Check the USB cable and allow USB debugging "
                      "on the phone, then try again.");
        }
        return tr("This computer could not open the connection. Try again, or restart the "
                  "computer if it keeps failing.");
    case ErrorCode::Rejected:
    case ErrorCode::ProtocolError:
    case ErrorCode::LimitExceeded:
        break;
    }
    return tr("The phone sent an answer QPhoneManager does not understand. Update "
              "MyPhoneExplorer Client on the phone, then try again.");
}

} // namespace qpm::ui
