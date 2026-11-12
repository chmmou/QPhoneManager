#include "ui/Settings.h"

#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using qpm::ui::Settings;

class TestSettings : public QObject
{
    Q_OBJECT

public:
    explicit TestSettings(QObject *parent = nullptr) : QObject(parent) {}

private Q_SLOTS:
    void init();
    void cleanup();

    void freshSettingsHaveTheDefaults();
    void changesSurviveANewInstance();
    void setterSignalsOnlyRealChanges();
    void relativeAdbPathIsIgnored();
    void relativeDataDirectoryIsIgnored();
    void onlyLocalFilesGiveAPath();
    void connectionOrderIsAlwaysAPermutation_data();
    void connectionOrderIsAlwaysAPermutation();
    void unknownLanguageIsIgnored();
    void holidayRegionIsStoredAndUnknownOnesAreIgnored();
    void malformedFileValuesFallBackToDefaults();
    void settingsFileHoldsNoPin();

private:
    [[nodiscard]] QString fileName() const;

    std::unique_ptr<QTemporaryDir> m_dir;
};

void TestSettings::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

void TestSettings::cleanup()
{
    m_dir.reset();
}

QString TestSettings::fileName() const
{
    return m_dir->filePath(QStringLiteral("settings.ini"));
}

void TestSettings::freshSettingsHaveTheDefaults()
{
    const Settings settings(fileName());

    QVERIFY(settings.adbPath().isEmpty());
    QCOMPARE(settings.connectionOrder(),
             (QStringList{QStringLiteral("usb"), QStringLiteral("wifi"), QStringLiteral("bluetooth")}));
    QVERIFY(settings.autoConnect());
    QVERIFY(!settings.listenForPhone());
    QVERIFY(settings.language().isEmpty());
    QCOMPARE(settings.colorScheme(), Settings::ColorScheme::System);
    QVERIFY(!settings.reduceMotion());
    QVERIFY(settings.notificationsEnabled());
    QVERIFY(!settings.clipboardSync());
    QVERIFY(settings.dataDirectory().isEmpty());
    QVERIFY(settings.lastDevice().isEmpty());
}

void TestSettings::changesSurviveANewInstance()
{
    {
        Settings settings(fileName());
        settings.setAdbPath(QStringLiteral("/opt/android/platform-tools/adb"));
        settings.setConnectionOrder(
            {QStringLiteral("wifi"), QStringLiteral("usb"), QStringLiteral("bluetooth")});
        settings.setAutoConnect(false);
        settings.setListenForPhone(true);
        settings.setLanguage(QStringLiteral("en"));
        settings.setColorScheme(Settings::ColorScheme::Dark);
        settings.setReduceMotion(true);
        settings.setNotificationsEnabled(false);
        settings.setClipboardSync(true);
        settings.setDataDirectory(QStringLiteral("/data/phones/"));
        settings.setLastDevice(QStringLiteral("dev-1"));
    }

    const Settings settings(fileName());

    QCOMPARE(settings.adbPath(), QStringLiteral("/opt/android/platform-tools/adb"));
    QCOMPARE(settings.connectionOrder(),
             (QStringList{QStringLiteral("wifi"), QStringLiteral("usb"), QStringLiteral("bluetooth")}));
    QVERIFY(!settings.autoConnect());
    QVERIFY(settings.listenForPhone());
    QCOMPARE(settings.language(), QStringLiteral("en"));
    QCOMPARE(settings.colorScheme(), Settings::ColorScheme::Dark);
    QVERIFY(settings.reduceMotion());
    QVERIFY(!settings.notificationsEnabled());
    QVERIFY(settings.clipboardSync());
    QCOMPARE(settings.dataDirectory(), QStringLiteral("/data/phones"));
    QCOMPARE(settings.lastDevice(), QStringLiteral("dev-1"));
}

void TestSettings::setterSignalsOnlyRealChanges()
{
    Settings settings(fileName());
    QSignalSpy schemeSpy(&settings, &Settings::colorSchemeChanged);
    QSignalSpy autoConnectSpy(&settings, &Settings::autoConnectChanged);
    QSignalSpy languageSpy(&settings, &Settings::languageChanged);

    settings.setColorScheme(Settings::ColorScheme::System);
    settings.setColorScheme(Settings::ColorScheme::Light);
    settings.setColorScheme(Settings::ColorScheme::Light);
    settings.setAutoConnect(true);
    settings.setAutoConnect(false);
    settings.setLanguage(QString());
    settings.setLanguage(QStringLiteral("de"));

    QCOMPARE(schemeSpy.count(), 1);
    QCOMPARE(autoConnectSpy.count(), 1);
    QCOMPARE(languageSpy.count(), 1);
}

void TestSettings::relativeAdbPathIsIgnored()
{
    Settings settings(fileName());
    settings.setAdbPath(QStringLiteral("/usr/bin/adb"));
    QSignalSpy spy(&settings, &Settings::adbPathChanged);

    settings.setAdbPath(QStringLiteral("adb"));
    settings.setAdbPath(QStringLiteral("../bin/adb"));

    QCOMPARE(settings.adbPath(), QStringLiteral("/usr/bin/adb"));
    QCOMPARE(spy.count(), 0);
}

void TestSettings::relativeDataDirectoryIsIgnored()
{
    Settings settings(fileName());
    settings.setDataDirectory(QStringLiteral("/data/phones"));
    QSignalSpy spy(&settings, &Settings::dataDirectoryChanged);

    settings.setDataDirectory(QStringLiteral("phones"));
    settings.setDataDirectory(QStringLiteral("/data/phones/."));

    QCOMPARE(settings.dataDirectory(), QStringLiteral("/data/phones"));
    QCOMPARE(spy.count(), 0);
    settings.setDataDirectory(QString());
    QVERIFY(settings.dataDirectory().isEmpty());
    QCOMPARE(spy.count(), 1);
}

void TestSettings::onlyLocalFilesGiveAPath()
{
    const Settings settings(fileName());
    QCOMPARE(settings.pathFromUrl(QUrl::fromLocalFile(QStringLiteral("/opt/sdk/adb"))),
             QStringLiteral("/opt/sdk/adb"));
    QCOMPARE(settings.pathFromUrl(QUrl(QStringLiteral("file:///home/a%20b/x"))),
             QStringLiteral("/home/a b/x"));
    QVERIFY(settings.pathFromUrl(QUrl(QStringLiteral("https://example.org/adb"))).isEmpty());
    QVERIFY(settings.pathFromUrl(QUrl()).isEmpty());
}

void TestSettings::connectionOrderIsAlwaysAPermutation_data()
{
    QTest::addColumn<QStringList>("input");
    QTest::addColumn<QStringList>("expected");

    const QString usb = QStringLiteral("usb");
    const QString wifi = QStringLiteral("wifi");
    const QString bluetooth = QStringLiteral("bluetooth");
    QTest::newRow("unknown entry") << QStringList{bluetooth, QStringLiteral("modem"), usb}
                                   << QStringList{bluetooth, usb, wifi};
    QTest::newRow("repeated entry") << QStringList{wifi, wifi, usb}
                                    << QStringList{wifi, usb, bluetooth};
    QTest::newRow("empty") << QStringList() << QStringList{usb, wifi, bluetooth};
}

void TestSettings::connectionOrderIsAlwaysAPermutation()
{
    QFETCH(QStringList, input);
    QFETCH(QStringList, expected);
    Settings settings(fileName());

    settings.setConnectionOrder(input);

    QCOMPARE(settings.connectionOrder(), expected);
}

void TestSettings::unknownLanguageIsIgnored()
{
    Settings settings(fileName());
    settings.setLanguage(QStringLiteral("de"));

    settings.setLanguage(QStringLiteral("fr"));
    settings.setLanguage(QStringLiteral("../de"));

    QCOMPARE(settings.language(), QStringLiteral("de"));
}

void TestSettings::holidayRegionIsStoredAndUnknownOnesAreIgnored()
{
    {
        Settings settings(fileName());
        QCOMPARE(settings.holidayRegion(), QString());
        QSignalSpy changed(&settings, &Settings::holidayRegionChanged);

        settings.setHolidayRegion(QStringLiteral("ch"));
        settings.setHolidayRegion(QStringLiteral("ch"));
        settings.setHolidayRegion(QStringLiteral("fr"));

        QCOMPARE(settings.holidayRegion(), QStringLiteral("ch"));
        QCOMPARE(changed.count(), 1);
    }
    {
        QSettings raw(fileName(), QSettings::IniFormat);
        QCOMPARE(raw.value(QStringLiteral("calendar/holidayRegion")).toString(), QStringLiteral("ch"));
        raw.setValue(QStringLiteral("calendar/holidayRegion"), QStringLiteral("../de"));
    }
    const Settings settings(fileName());
    QCOMPARE(settings.holidayRegion(), QString());
}

void TestSettings::malformedFileValuesFallBackToDefaults()
{
    {
        QSettings raw(fileName(), QSettings::IniFormat);
        raw.setValue(QStringLiteral("connection/adbPath"), QStringLiteral("adb; rm -rf ~"));
        raw.setValue(QStringLiteral("connection/order"),
                     QStringList{QStringLiteral("modem"), QStringLiteral("wifi")});
        raw.setValue(QStringLiteral("appearance/language"), QStringLiteral("klingon"));
        raw.setValue(QStringLiteral("appearance/colorScheme"), QStringLiteral("purple"));
    }

    const Settings settings(fileName());

    QVERIFY(settings.adbPath().isEmpty());
    QCOMPARE(settings.connectionOrder(),
             (QStringList{QStringLiteral("wifi"), QStringLiteral("usb"), QStringLiteral("bluetooth")}));
    QVERIFY(settings.language().isEmpty());
    QCOMPARE(settings.colorScheme(), Settings::ColorScheme::System);
}

void TestSettings::settingsFileHoldsNoPin()
{
    {
        Settings settings(fileName());
        settings.setAdbPath(QStringLiteral("/usr/bin/adb"));
        settings.setColorScheme(Settings::ColorScheme::Dark);
        settings.setListenForPhone(true);
    }

    const QSettings raw(fileName(), QSettings::IniFormat);
    const QStringList keys = raw.allKeys();
    QVERIFY(!keys.isEmpty());
    for (const QString &key : keys)
        QVERIFY2(!key.contains(QStringLiteral("pin"), Qt::CaseInsensitive), qPrintable(key));
}

QTEST_GUILESS_MAIN(TestSettings)

#include "tst_settings.moc"
