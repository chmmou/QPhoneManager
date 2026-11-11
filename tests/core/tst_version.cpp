#include "core/Version.h"

#include <QTest>

class TestVersion : public QObject
{
    Q_OBJECT

public:
    explicit TestVersion(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void applicationVersionMatchesProjectVersion();
};

void TestVersion::applicationVersionMatchesProjectVersion()
{
    QCOMPARE(qpm::applicationVersion(), QStringLiteral("1.0.0"));
}

QTEST_GUILESS_MAIN(TestVersion)

#include "tst_version.moc"
