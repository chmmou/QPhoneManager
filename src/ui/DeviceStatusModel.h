#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::core::service {
class DeviceStatusService;
}

namespace qpm::ui {

/**
 * The overview of the connected phone, prepared for display: texts are
 * translated and formatted for the current locale. A number property is -1
 * and a text property empty while the phone has not reported the value.
 *
 * Mirrors a DeviceStatusService, which must outlive it; every property
 * changes together through changed().
 */
class DeviceStatusModel : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DeviceStatusModel)
    QML_UNCREATABLE("Provided by AppController")

    Q_PROPERTY(bool hasData READ hasData NOTIFY changed FINAL)
    /// Manufacturer and model, without repeating the manufacturer.
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed FINAL)
    Q_PROPERTY(QString manufacturer READ manufacturer NOTIFY changed FINAL)
    /// For example "Android 14 (Upside Down Cake)".
    Q_PROPERTY(QString androidVersion READ androidVersion NOTIFY changed FINAL)
    Q_PROPERTY(QString clientVersion READ clientVersion NOTIFY changed FINAL)
    Q_PROPERTY(QString firmware READ firmware NOTIFY changed FINAL)
    /// 0 to 100.
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY changed FINAL)
    Q_PROPERTY(bool charging READ isCharging NOTIFY changed FINAL)
    /// For example "24 °C".
    Q_PROPERTY(QString batteryTemperature READ batteryTemperature NOTIFY changed FINAL)
    /// For example "3.92 V".
    Q_PROPERTY(QString batteryVoltage READ batteryVoltage NOTIFY changed FINAL)
    /// 0 to 4 bars.
    Q_PROPERTY(int signalBars READ signalBars NOTIFY changed FINAL)
    /// "No signal", "Weak", "Fair", "Good" or "Very good".
    Q_PROPERTY(QString signalText READ signalText NOTIFY changed FINAL)
    Q_PROPERTY(QString operatorName READ operatorName NOTIFY changed FINAL)
    /// One line per SIM card, for example "SIM 1: Work (Example Net)".
    Q_PROPERTY(QStringList simCards READ simCards NOTIFY changed FINAL)
    Q_PROPERTY(bool simPermissionMissing READ simPermissionMissing NOTIFY changed FINAL)
    /// 0 to 100.
    Q_PROPERTY(int cpuPercent READ cpuPercent NOTIFY changed FINAL)
    /// For example "32 GB of 64 GB free".
    Q_PROPERTY(QString internalStorage READ internalStorage NOTIFY changed FINAL)
    /// Used share of the internal storage from 0 to 1, or -1.
    Q_PROPERTY(double internalStorageUsed READ internalStorageUsed NOTIFY changed FINAL)
    Q_PROPERTY(QString cardStorage READ cardStorage NOTIFY changed FINAL)
    Q_PROPERTY(double cardStorageUsed READ cardStorageUsed NOTIFY changed FINAL)
    /// For example "1080 x 2400 pixels, 420 dpi".
    Q_PROPERTY(QString display READ display NOTIFY changed FINAL)

public:
    explicit DeviceStatusModel(const core::service::DeviceStatusService *service,
                               QObject *parent = nullptr);

    [[nodiscard]] bool hasData() const;
    [[nodiscard]] QString deviceName() const;
    [[nodiscard]] QString manufacturer() const;
    [[nodiscard]] QString androidVersion() const;
    [[nodiscard]] QString clientVersion() const;
    [[nodiscard]] QString firmware() const;
    [[nodiscard]] int batteryPercent() const;
    [[nodiscard]] bool isCharging() const;
    [[nodiscard]] QString batteryTemperature() const;
    [[nodiscard]] QString batteryVoltage() const;
    [[nodiscard]] int signalBars() const;
    [[nodiscard]] QString signalText() const;
    [[nodiscard]] QString operatorName() const;
    [[nodiscard]] QStringList simCards() const;
    [[nodiscard]] bool simPermissionMissing() const;
    [[nodiscard]] int cpuPercent() const;
    [[nodiscard]] QString internalStorage() const;
    [[nodiscard]] double internalStorageUsed() const;
    [[nodiscard]] QString cardStorage() const;
    [[nodiscard]] double cardStorageUsed() const;
    [[nodiscard]] QString display() const;

    /// Bars for a signal value as AT+CSQ reports it: 0 to 31, 99 unknown.
    [[nodiscard]] static int barsForSignal(int signal);

Q_SIGNALS:
    void changed();

private:
    [[nodiscard]] QString storageText(const QString &root) const;
    [[nodiscard]] double storageUsed(const QString &root) const;

    const core::service::DeviceStatusService *m_service = nullptr;
};

} // namespace qpm::ui
