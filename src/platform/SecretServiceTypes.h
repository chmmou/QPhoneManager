#pragma once

#include <QByteArray>
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QMap>
#include <QMetaType>
#include <QString>

namespace qpm {

/// The well-known bus name of the freedesktop Secret Service.
inline constexpr char secretServiceName[] = "org.freedesktop.secrets";

/**
 * The Secret structure of the freedesktop Secret Service API, D-Bus
 * signature (oayays). With the "plain" algorithm `parameters` is empty and
 * `value` holds the secret unencrypted.
 */
struct SecretServiceSecret
{
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

/// The answer of GetSecrets: one secret per item.
using SecretServiceSecretMap = QMap<QDBusObjectPath, SecretServiceSecret>;

/// The attributes an item is found by, D-Bus signature a{ss}.
using SecretServiceAttributes = QMap<QString, QString>;

QDBusArgument &operator<<(QDBusArgument &argument, const SecretServiceSecret &secret);
const QDBusArgument &operator>>(const QDBusArgument &argument, SecretServiceSecret &secret);

/**
 * Registers the types above with Qt D-Bus. Must run before the first call
 * that sends or receives them; calling it again does nothing.
 */
void registerSecretServiceTypes();

} // namespace qpm

Q_DECLARE_METATYPE(qpm::SecretServiceSecret)
