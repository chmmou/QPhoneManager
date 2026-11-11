#include "core/Version.h"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>

#include <cstdlib>

Q_IMPORT_QML_PLUGIN(QPhoneManager_UiPlugin)

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("QPhoneManager"));
    QCoreApplication::setApplicationName(QStringLiteral("QPhoneManager"));
    QCoreApplication::setApplicationVersion(qpm::applicationVersion());

    QApplication app(argc, argv);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule("QPhoneManager.Ui", "Main");

    return QApplication::exec();
}
