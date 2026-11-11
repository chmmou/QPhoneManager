#pragma once

#include "FakePhone.h"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace qpm::test {

/**
 * The fake phone for QML tests, listening on a free port of the loopback
 * address for as long as the QML engine runs.
 */
class TestPhone : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(TestPhone)
    QML_SINGLETON

    /// "127.0.0.1:<port>", as the connect dialog takes it.
    Q_PROPERTY(QString address READ address CONSTANT FINAL)

public:
    explicit TestPhone(QObject *parent = nullptr);

    [[nodiscard]] QString address() const;

    /// Requires `pin` from the next connection on; empty turns the PIN off.
    Q_INVOKABLE void setPin(const QString &pin);

private:
    FakePhone m_phone;
};

} // namespace qpm::test
