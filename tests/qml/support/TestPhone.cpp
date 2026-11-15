#include "TestPhone.h"

#include <QLoggingCategory>

namespace qpm::test {

namespace {
Q_LOGGING_CATEGORY(lcTestPhone, "qpm.test.phone")
}

TestPhone::TestPhone(QObject *parent) : QObject(parent)
{
    if (!m_phone.listen())
        qCWarning(lcTestPhone) << "The fake phone cannot listen; the QML tests that connect will fail";
}

QString TestPhone::address() const
{
    return QStringLiteral("127.0.0.1:%1").arg(m_phone.port());
}

void TestPhone::setPin(const QString &pin)
{
    m_phone.config().pin = pin;
}

void TestPhone::setRejectAsAlreadyConnected(bool reject)
{
    m_phone.config().rejectAsAlreadyConnected = reject;
}

} // namespace qpm::test
