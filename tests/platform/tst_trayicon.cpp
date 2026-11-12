#include "platform/TrayIcon.h"

#include <QAction>
#include <QMenu>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTest>

using qpm::TrayIcon;

namespace {

QList<QAction *> entries(const TrayIcon &tray)
{
    QList<QAction *> found;
    const QList<QAction *> actions = tray.menu()->actions();
    for (QAction *action : actions) {
        if (!action->isSeparator())
            found.append(action);
    }
    return found;
}

} // namespace

// The icon in the notification area: its menu, which entries need a phone,
// and that the window stays visible where the desktop has no such area.
class TestTrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TestTrayIcon(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void menuHasItsFourEntriesInOrder();
    void entriesNeedingAPhoneAreDisabledNotHidden();
    void everyEntryAsksForItsAction();
    void windowStartsHiddenOnlyWithANotificationArea_data();
    void windowStartsHiddenOnlyWithANotificationArea();
    void showingReportsWhetherTheIconIsThere();
};

void TestTrayIcon::menuHasItsFourEntriesInOrder()
{
    const TrayIcon tray(QIcon{});

    const QList<QAction *> found = entries(tray);

    QCOMPARE(found.size(), 4);
    QCOMPARE(found.at(0)->text(), QStringLiteral("Show window"));
    QCOMPARE(found.at(1)->text(), QStringLiteral("Write message"));
    QCOMPARE(found.at(2)->text(), QStringLiteral("Disconnect"));
    QCOMPARE(found.at(3)->text(), QStringLiteral("Quit"));
}

void TestTrayIcon::entriesNeedingAPhoneAreDisabledNotHidden()
{
    TrayIcon tray(QIcon{});
    const QList<QAction *> found = entries(tray);

    QVERIFY(found.at(0)->isEnabled());
    QVERIFY(!found.at(1)->isEnabled());
    QVERIFY(!found.at(2)->isEnabled());
    QVERIFY(found.at(3)->isEnabled());
    tray.setConnected(true);
    QVERIFY(found.at(1)->isEnabled());
    QVERIFY(found.at(2)->isEnabled());
    tray.setConnected(false);
    QVERIFY(!found.at(1)->isEnabled());
    for (const QAction *action : found)
        QVERIFY(action->isVisible());
}

void TestTrayIcon::everyEntryAsksForItsAction()
{
    TrayIcon tray(QIcon{});
    tray.setConnected(true);
    QSignalSpy show(&tray, &TrayIcon::showWindowRequested);
    QSignalSpy write(&tray, &TrayIcon::writeMessageRequested);
    QSignalSpy disconnect(&tray, &TrayIcon::disconnectRequested);
    QSignalSpy quit(&tray, &TrayIcon::quitRequested);
    const QList<QAction *> found = entries(tray);

    for (QAction *action : found)
        action->trigger();

    QCOMPARE(show.count(), 1);
    QCOMPARE(write.count(), 1);
    QCOMPARE(disconnect.count(), 1);
    QCOMPARE(quit.count(), 1);
}

void TestTrayIcon::windowStartsHiddenOnlyWithANotificationArea_data()
{
    QTest::addColumn<bool>("startMinimized");
    QTest::addColumn<bool>("trayAvailable");
    QTest::addColumn<bool>("hidden");

    QTest::newRow("asked, area there") << true << true << true;
    QTest::newRow("asked, no area") << true << false << false;
    QTest::newRow("not asked, area there") << false << true << false;
    QTest::newRow("not asked, no area") << false << false << false;
}

void TestTrayIcon::windowStartsHiddenOnlyWithANotificationArea()
{
    QFETCH(bool, startMinimized);
    QFETCH(bool, trayAvailable);
    QFETCH(bool, hidden);

    QCOMPARE(TrayIcon::shouldStartHidden(startMinimized, trayAvailable), hidden);
}

void TestTrayIcon::showingReportsWhetherTheIconIsThere()
{
    TrayIcon tray(QIcon{});

    const bool shown = tray.show();

    QCOMPARE(shown, QSystemTrayIcon::isSystemTrayAvailable());
    if (!shown)
        QVERIFY(!tray.isShown());
}

QTEST_MAIN(TestTrayIcon)

#include "tst_trayicon.moc"
