#include "core/Logging.h"
#include "core/Version.h"
#include "platform/TrayIcon.h"
#include "ui/AppController.h"
#include "ui/ReducedMotionFilter.h"
#include "ui/Settings.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
#include <QQuickWindow>
#include <QTranslator>

#include <cstdlib>

Q_IMPORT_QML_PLUGIN(QPhoneManager_UiPlugin)

namespace {

// German is the main language of the program, so it is also the choice when
// the system asks for neither German nor English.
QString uiLanguage(const QString &setting)
{
    if (!setting.isEmpty())
        return setting;
    const QStringList tags = QLocale::system().uiLanguages();
    for (const QString &tag : tags) {
        const QString language = tag.section(u'-', 0, 0).toLower();
        if (language == QLatin1StringView("de") || language == QLatin1StringView("en"))
            return language;
    }
    return QStringLiteral("de");
}

void installCatalogue(QTranslator &translator, const QString &name, const QString &language)
{
    const QString file = QStringLiteral(":/i18n/%1_%2.qm").arg(name, language);
    if (!translator.load(file)) {
        // The texts then stay in English, which still leaves the program usable.
        qCWarning(qpm::core::lcUi) << "Cannot load the translation" << file;
        return;
    }
    QCoreApplication::installTranslator(&translator);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("QPhoneManager"));
    QCoreApplication::setApplicationName(QStringLiteral("QPhoneManager"));
    QCoreApplication::setApplicationVersion(qpm::applicationVersion());
    // The name of the installed desktop file. Wayland and the task bar find
    // the entry and its icon through it.
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.chmmou.QPhoneManager"));

    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app/qphonemanager.svg")));

    const qpm::ui::Settings startup;
    const QString language = uiLanguage(startup.language());
    // Numbers, dates and sizes follow the language of the texts; the region
    // of the system stays when it already speaks that language.
    if (QLocale::system().name().section(u'_', 0, 0) != language)
        QLocale::setDefault(QLocale(language));
    // The settings are read anew at each request, so a change shows at once.
    qpm::ui::ReducedMotionFilter reducedMotion(&startup);
    app.installEventFilter(&reducedMotion);
    QTranslator appTranslator;
    QTranslator qtBaseTranslator;
    QTranslator qtQuickTranslator;
    installCatalogue(appTranslator, QStringLiteral("qphonemanager"), language);
    installCatalogue(qtBaseTranslator, QStringLiteral("qtbase"), language);
    installCatalogue(qtQuickTranslator, QStringLiteral("qtdeclarative"), language);

    qpm::TrayIcon tray(QApplication::windowIcon());
    const bool trayShown = tray.show();
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    if (qpm::TrayIcon::shouldStartHidden(startup.startMinimized(), trayShown))
        engine.setInitialProperties({{QStringLiteral("visible"), false}});
    engine.loadFromModule("QPhoneManager.Ui", "Main");

    auto *controller =
        engine.singletonInstance<qpm::ui::AppController *>("QPhoneManager.Ui", "AppController");
    auto *window = engine.rootObjects().isEmpty()
                       ? nullptr
                       : qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (controller && window) {
        tray.setConnected(controller->isConnected());
        QObject::connect(controller, &qpm::ui::AppController::connectionStateChanged, &tray,
                         [&tray, controller]() { tray.setConnected(controller->isConnected()); });
        QObject::connect(&tray, &qpm::TrayIcon::showWindowRequested, window, [window]() {
            window->show();
            window->raise();
            window->requestActivate();
        });
        QObject::connect(&tray, &qpm::TrayIcon::writeMessageRequested, controller,
                         &qpm::ui::AppController::requestNewMessage);
        QObject::connect(&tray, &qpm::TrayIcon::disconnectRequested, controller,
                         &qpm::ui::AppController::disconnectPhone);
    }
    QObject::connect(&tray, &qpm::TrayIcon::quitRequested, &app, &QCoreApplication::quit);

    return QApplication::exec();
}
