#include "ui/DeviceStatusModel.h"

#include "core/service/DeviceStatusService.h"

#include <QLocale>

namespace qpm::ui {

namespace {

using core::service::DeviceOverview;
using core::service::StorageSpace;

constexpr int unknownSignal = 99;
constexpr int maxBars = 4;
constexpr double millivoltsPerVolt = 1000.0;

const StorageSpace *findRoot(const DeviceOverview &overview, const QString &root)
{
    for (const StorageSpace &space : overview.storage) {
        if (space.root == root)
            return &space;
    }
    return nullptr;
}

QString sdcardRoot()
{
    return QStringLiteral("sdcard");
}

QString externalRoot()
{
    return QStringLiteral("external");
}

} // namespace

DeviceStatusModel::DeviceStatusModel(const core::service::DeviceStatusService *service,
                                     QObject *parent)
    : QObject(parent), m_service(service)
{
    Q_ASSERT(service);
    connect(service, &core::service::DeviceStatusService::changed, this,
            &DeviceStatusModel::changed);
}

bool DeviceStatusModel::hasData() const
{
    return m_service->hasData();
}

QString DeviceStatusModel::deviceName() const
{
    const QString model = m_service->overview().identity.model;
    const QString maker = manufacturer();
    if (maker.isEmpty() || model.startsWith(maker, Qt::CaseInsensitive))
        return model;
    return tr("%1 %2", "manufacturer and model").arg(maker, model);
}

QString DeviceStatusModel::manufacturer() const
{
    return m_service->overview().identity.manufacturer;
}

QString DeviceStatusModel::androidVersion() const
{
    const core::protocol::DeviceIdentity &identity = m_service->overview().identity;
    if (identity.androidVersion.isEmpty())
        return {};
    if (identity.androidCodename.isEmpty())
        return identity.androidVersion;
    return tr("%1 (%2)", "Android version and its code name")
        .arg(identity.androidVersion, identity.androidCodename);
}

QString DeviceStatusModel::clientVersion() const
{
    return m_service->overview().identity.clientVersion;
}

QString DeviceStatusModel::firmware() const
{
    return m_service->overview().firmware;
}

int DeviceStatusModel::batteryPercent() const
{
    return m_service->overview().status.batteryPercent.value_or(-1);
}

bool DeviceStatusModel::isCharging() const
{
    return m_service->overview().status.charging.value_or(false);
}

QString DeviceStatusModel::batteryTemperature() const
{
    const std::optional<int> celsius = m_service->overview().status.temperatureCelsius;
    if (!celsius)
        return {};
    return tr("%1 °C").arg(QLocale().toString(*celsius));
}

QString DeviceStatusModel::batteryVoltage() const
{
    const std::optional<int> millivolts = m_service->overview().status.voltageMillivolts;
    if (!millivolts || *millivolts == 0)
        return {};
    return tr("%1 V").arg(QLocale().toString(*millivolts / millivoltsPerVolt, 'f', 2));
}

int DeviceStatusModel::barsForSignal(int signal)
{
    if (signal < 0 || signal == unknownSignal || signal > 31)
        return -1;
    if (signal == 0)
        return 0;
    if (signal < 10)
        return 1;
    if (signal < 15)
        return 2;
    if (signal < 20)
        return 3;
    return maxBars;
}

int DeviceStatusModel::signalBars() const
{
    return barsForSignal(m_service->overview().status.signal.value_or(-1));
}

QString DeviceStatusModel::signalText() const
{
    switch (signalBars()) {
    case 0:
        return tr("No signal");
    case 1:
        return tr("Weak");
    case 2:
        return tr("Fair");
    case 3:
        return tr("Good");
    case maxBars:
        return tr("Very good");
    default:
        break;
    }
    return {};
}

QString DeviceStatusModel::operatorName() const
{
    const DeviceOverview &overview = m_service->overview();
    return overview.networkOperator ? overview.networkOperator->name : QString();
}

QStringList DeviceStatusModel::simCards() const
{
    QStringList lines;
    for (const core::service::SimCard &sim : m_service->overview().sims) {
        const QString name = sim.name.isEmpty() ? sim.carrier : sim.name;
        if (sim.carrier.isEmpty() || sim.carrier == name)
            lines.append(tr("SIM %1: %2").arg(sim.slot + 1).arg(name));
        else
            lines.append(tr("SIM %1: %2 (%3)").arg(sim.slot + 1).arg(name, sim.carrier));
    }
    return lines;
}

bool DeviceStatusModel::simPermissionMissing() const
{
    return m_service->overview().simPermissionMissing;
}

int DeviceStatusModel::cpuPercent() const
{
    return m_service->overview().status.cpuPercent.value_or(-1);
}

QString DeviceStatusModel::storageText(const QString &root) const
{
    const StorageSpace *space = findRoot(m_service->overview(), root);
    if (!space)
        return {};
    const QLocale locale;
    return tr("%1 of %2 free")
        .arg(locale.formattedDataSize(qint64(space->freeBytes), 1, QLocale::DataSizeSIFormat),
             locale.formattedDataSize(qint64(space->totalBytes), 1, QLocale::DataSizeSIFormat));
}

double DeviceStatusModel::storageUsed(const QString &root) const
{
    const StorageSpace *space = findRoot(m_service->overview(), root);
    if (!space)
        return -1;
    return double(space->totalBytes - space->freeBytes) / double(space->totalBytes);
}

QString DeviceStatusModel::internalStorage() const
{
    return storageText(sdcardRoot());
}

double DeviceStatusModel::internalStorageUsed() const
{
    return storageUsed(sdcardRoot());
}

QString DeviceStatusModel::cardStorage() const
{
    return storageText(externalRoot());
}

double DeviceStatusModel::cardStorageUsed() const
{
    return storageUsed(externalRoot());
}

QString DeviceStatusModel::display() const
{
    const std::optional<core::service::DisplayInfo> info = m_service->overview().display;
    if (!info)
        return {};
    // A resolution reads as two plain numbers, without digit grouping.
    if (info->densityDpi <= 0)
        return tr("%1 x %2 pixels").arg(info->width).arg(info->height);
    return tr("%1 x %2 pixels, %3 dpi").arg(info->width).arg(info->height).arg(info->densityDpi);
}

} // namespace qpm::ui
