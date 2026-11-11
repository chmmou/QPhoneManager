#include "core/Logging.h"
#include "core/Version.h"
#include "ui/Settings.h"

#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlExtensionPlugin>
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

    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app/qphonemanager.svg")));

    const QString language = uiLanguage(qpm::ui::Settings().language());
    QTranslator appTranslator;
    QTranslator qtBaseTranslator;
    QTranslator qtQuickTranslator;
    installCatalogue(appTranslator, QStringLiteral("qphonemanager"), language);
    installCatalogue(qtBaseTranslator, QStringLiteral("qtbase"), language);
    installCatalogue(qtQuickTranslator, QStringLiteral("qtdeclarative"), language);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(EXIT_FAILURE); }, Qt::QueuedConnection);
    engine.loadFromModule("QPhoneManager.Ui", "Main");

    return QApplication::exec();
}
