#include "ui/ReducedMotionFilter.h"
#include "ui/Settings.h"

#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QTemporaryDir>
#include <QtQuickTest/quicktest.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(QPhoneManager_UiPlugin)
Q_IMPORT_QML_PLUGIN(QPhoneManager_TestSupportPlugin)

// Keeps the settings and phone data of the QML tests in a folder of their
// own, away from those of the user who runs them.
class Setup : public QObject
{
    Q_OBJECT

public:
    explicit Setup(QObject *parent = nullptr) : QObject(parent) {}

public Q_SLOTS:
    void applicationAvailable()
    {
        m_home = std::make_unique<QTemporaryDir>();
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_home->filePath(QStringLiteral("config"))));
        qputenv("XDG_DATA_HOME", QFile::encodeName(m_home->filePath(QStringLiteral("data"))));
        QCoreApplication::setOrganizationName(QStringLiteral("QPhoneManagerTest"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_qml"));
        QLocale::setDefault(QLocale::c());
        // As in the program, so that the text menus follow the setting.
        m_settings = std::make_unique<qpm::ui::Settings>();
        m_reducedMotion = std::make_unique<qpm::ui::ReducedMotionFilter>(m_settings.get());
        QCoreApplication::instance()->installEventFilter(m_reducedMotion.get());
    }

private:
    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<qpm::ui::Settings> m_settings;
    std::unique_ptr<qpm::ui::ReducedMotionFilter> m_reducedMotion;
};

QUICK_TEST_MAIN_WITH_SETUP(qml, Setup)

#include "tst_qml.moc"
