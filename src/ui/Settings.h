#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QSettings;

namespace qpm::ui {

/**
 * The program settings, stored with QSettings. Never holds the PIN; that
 * lives in the Secret Service only.
 *
 * Every setter keeps a valid value: unknown or malformed input is ignored or
 * corrected as described at the property, and values read from the settings
 * file pass the same checks. A change is written immediately.
 */
class Settings : public QObject
{
    Q_OBJECT
    // Named explicitly: the type registrar of Qt 6.12 otherwise warns that
    // the namespaced class name starts with a lowercase letter.
    QML_NAMED_ELEMENT(Settings)
    QML_SINGLETON

    /// Absolute path of adb; empty means "look for it". A relative path is ignored.
    Q_PROPERTY(QString adbPath READ adbPath WRITE setAdbPath NOTIFY adbPathChanged FINAL)
    /**
     * The order in which connection kinds are tried: a permutation of "usb",
     * "wifi" and "bluetooth". Unknown and repeated entries are dropped and
     * missing ones appended in that default order.
     */
    Q_PROPERTY(QStringList connectionOrder READ connectionOrder WRITE setConnectionOrder NOTIFY
                   connectionOrderChanged FINAL)
    /// Connect to the last phone on start-up.
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY autoConnectChanged FINAL)
    /**
     * Wait for the phone to connect to this computer even while no QR code
     * is shown. Off by default.
     */
    Q_PROPERTY(bool listenForPhone READ listenForPhone WRITE setListenForPhone NOTIFY
                   listenForPhoneChanged FINAL)
    /// "de", "en", or empty for the system language. Other values are ignored.
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)
    Q_PROPERTY(ColorScheme colorScheme READ colorScheme WRITE setColorScheme NOTIFY
                   colorSchemeChanged FINAL)
    Q_PROPERTY(bool reduceMotion READ reduceMotion WRITE setReduceMotion NOTIFY reduceMotionChanged FINAL)
    Q_PROPERTY(bool notificationsEnabled READ notificationsEnabled WRITE setNotificationsEnabled
                   NOTIFY notificationsEnabledChanged FINAL)
    Q_PROPERTY(bool clipboardSync READ clipboardSync WRITE setClipboardSync NOTIFY
                   clipboardSyncChanged FINAL)

public:
    enum class ColorScheme { System, Light, Dark };
    Q_ENUM(ColorScheme)

    /// Uses the settings location of the application.
    explicit Settings(QObject *parent = nullptr);
    /// Uses the INI file `fileName`.
    explicit Settings(const QString &fileName, QObject *parent = nullptr);

    [[nodiscard]] QString adbPath() const;
    void setAdbPath(const QString &path);
    [[nodiscard]] QStringList connectionOrder() const;
    void setConnectionOrder(const QStringList &order);
    [[nodiscard]] bool autoConnect() const;
    void setAutoConnect(bool enabled);
    [[nodiscard]] bool listenForPhone() const;
    void setListenForPhone(bool enabled);
    [[nodiscard]] QString language() const;
    void setLanguage(const QString &language);
    [[nodiscard]] ColorScheme colorScheme() const;
    void setColorScheme(ColorScheme scheme);
    [[nodiscard]] bool reduceMotion() const;
    void setReduceMotion(bool enabled);
    [[nodiscard]] bool notificationsEnabled() const;
    void setNotificationsEnabled(bool enabled);
    [[nodiscard]] bool clipboardSync() const;
    void setClipboardSync(bool enabled);

Q_SIGNALS:
    void adbPathChanged();
    void connectionOrderChanged();
    void autoConnectChanged();
    void listenForPhoneChanged();
    void languageChanged();
    void colorSchemeChanged();
    void reduceMotionChanged();
    void notificationsEnabledChanged();
    void clipboardSyncChanged();

private:
    void setFlag(const QString &key, bool defaultValue, bool enabled, void (Settings::*changed)());

    QSettings *m_settings = nullptr;
};

} // namespace qpm::ui
