#include "platform/SecretServiceTypes.h"

#include <QDBusMetaType>

namespace qpm {

QDBusArgument &operator<<(QDBusArgument &argument, const SecretServiceSecret &secret)
{
    argument.beginStructure();
    argument << secret.session << secret.parameters << secret.value << secret.contentType;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SecretServiceSecret &secret)
{
    argument.beginStructure();
    argument >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
    argument.endStructure();
    return argument;
}

void registerSecretServiceTypes()
{
    static const bool registered = []() {
        qDBusRegisterMetaType<SecretServiceSecret>();
        qDBusRegisterMetaType<SecretServiceSecretMap>();
        qDBusRegisterMetaType<SecretServiceAttributes>();
        return true;
    }();
    Q_UNUSED(registered)
}

} // namespace qpm
