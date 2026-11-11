#include "ui/Settings.h"

#include <QDir>
#include <QSettings>

namespace qpm::ui {

namespace {

QString adbPathKey()
{
    return QStringLiteral("connection/adbPath");
}

QString connectionOrderKey()
{
    return QStringLiteral("connection/order");
}

QString autoConnectKey()
{
    return QStringLiteral("connection/autoConnect");
}

QString listenForPhoneKey()
{
    return QStringLiteral("connection/listenForPhone");
}

QString languageKey()
{
    return QStringLiteral("appearance/language");
}

QString colorSchemeKey()
{
    return QStringLiteral("appearance/colorScheme");
}

QString reduceMotionKey()
{
    return QStringLiteral("appearance/reduceMotion");
}

QString notificationsKey()
{
    return QStringLiteral("notifications/enabled");
}

QString clipboardSyncKey()
{
    return QStringLiteral("clipboard/sync");
}

QStringList defaultConnectionOrder()
{
    return {QStringLiteral("usb"), QStringLiteral("wifi"), QStringLiteral("bluetooth")};
}

QStringList normalizedOrder(const QStringList &order)
{
    const QStringList known = defaultConnectionOrder();
    QStringList result;
    for (const QString &entry : order) {
        if (known.contains(entry) && !result.contains(entry))
            result.append(entry);
    }
    for (const QString &entry : known) {
        if (!result.contains(entry))
            result.append(entry);
    }
    return result;
}

bool isKnownLanguage(const QString &language)
{
    return language.isEmpty() || language == QLatin1StringView("de") ||
           language == QLatin1StringView("en");
}

QString colorSchemeText(Settings::ColorScheme scheme)
{
    switch (scheme) {
    case Settings::ColorScheme::Light:
        return QStringLiteral("light");
    case Settings::ColorScheme::Dark:
        return QStringLiteral("dark");
    case Settings::ColorScheme::System:
        break;
    }
    return QStringLiteral("system");
}

} // namespace

Settings::Settings(QObject *parent) : QObject(parent), m_settings(new QSettings(this)) {}

Settings::Settings(const QString &fileName, QObject *parent)
    : QObject(parent), m_settings(new QSettings(fileName, QSettings::IniFormat, this))
{
}

QString Settings::adbPath() const
{
    const QString path = m_settings->value(adbPathKey()).toString();
    return QDir::isAbsolutePath(path) ? QDir::cleanPath(path) : QString();
}

void Settings::setAdbPath(const QString &path)
{
    if (!path.isEmpty() && !QDir::isAbsolutePath(path))
        return;
    const QString cleaned = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (cleaned == adbPath())
        return;
    m_settings->setValue(adbPathKey(), cleaned);
    Q_EMIT adbPathChanged();
}

QStringList Settings::connectionOrder() const
{
    return normalizedOrder(m_settings->value(connectionOrderKey()).toStringList());
}

void Settings::setConnectionOrder(const QStringList &order)
{
    const QStringList normalized = normalizedOrder(order);
    if (normalized == connectionOrder())
        return;
    m_settings->setValue(connectionOrderKey(), normalized);
    Q_EMIT connectionOrderChanged();
}

bool Settings::autoConnect() const
{
    return m_settings->value(autoConnectKey(), true).toBool();
}

void Settings::setAutoConnect(bool enabled)
{
    setFlag(autoConnectKey(), true, enabled, &Settings::autoConnectChanged);
}

bool Settings::listenForPhone() const
{
    return m_settings->value(listenForPhoneKey(), false).toBool();
}

void Settings::setListenForPhone(bool enabled)
{
    setFlag(listenForPhoneKey(), false, enabled, &Settings::listenForPhoneChanged);
}

QString Settings::language() const
{
    const QString language = m_settings->value(languageKey()).toString();
    return isKnownLanguage(language) ? language : QString();
}

void Settings::setLanguage(const QString &language)
{
    if (!isKnownLanguage(language) || language == this->language())
        return;
    m_settings->setValue(languageKey(), language);
    Q_EMIT languageChanged();
}

Settings::ColorScheme Settings::colorScheme() const
{
    const QString text = m_settings->value(colorSchemeKey()).toString();
    if (text == QLatin1StringView("light"))
        return ColorScheme::Light;
    if (text == QLatin1StringView("dark"))
        return ColorScheme::Dark;
    return ColorScheme::System;
}

void Settings::setColorScheme(ColorScheme scheme)
{
    if (scheme == colorScheme())
        return;
    m_settings->setValue(colorSchemeKey(), colorSchemeText(scheme));
    Q_EMIT colorSchemeChanged();
}

bool Settings::reduceMotion() const
{
    return m_settings->value(reduceMotionKey(), false).toBool();
}

void Settings::setReduceMotion(bool enabled)
{
    setFlag(reduceMotionKey(), false, enabled, &Settings::reduceMotionChanged);
}

bool Settings::notificationsEnabled() const
{
    return m_settings->value(notificationsKey(), true).toBool();
}

void Settings::setNotificationsEnabled(bool enabled)
{
    setFlag(notificationsKey(), true, enabled, &Settings::notificationsEnabledChanged);
}

bool Settings::clipboardSync() const
{
    return m_settings->value(clipboardSyncKey(), false).toBool();
}

void Settings::setClipboardSync(bool enabled)
{
    setFlag(clipboardSyncKey(), false, enabled, &Settings::clipboardSyncChanged);
}

void Settings::setFlag(const QString &key, bool defaultValue, bool enabled,
                       void (Settings::*changed)())
{
    if (m_settings->value(key, defaultValue).toBool() == enabled)
        return;
    m_settings->setValue(key, enabled);
    Q_EMIT(this->*changed)();
}

} // namespace qpm::ui
