// the launcher of powwow

#include "launcher.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#ifndef Q_OS_WIN
#  include <langinfo.h>
#  include <strings.h>
#endif
#include <clocale>

// does the terminal take UTF-8, as the locale says? asked before Qt, which
// switches a locale that is not UTF-8 to one that is
static bool utf8_locale()
{
#ifdef Q_OS_WIN
    return false;
#else
    setlocale(LC_CTYPE, "");
    const char *codeset = nl_langinfo(CODESET);
    return (strcasecmp(codeset, "UTF-8") == 0
            || strcasecmp(codeset, "UTF8") == 0);
#endif
}

int main(int argc, char *argv[])
{
    bool utf8 = utf8_locale();
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName("powwow");
    QGuiApplication::setApplicationName("powwow");
    QGuiApplication::setApplicationDisplayName("powwow");
    QGuiApplication::setApplicationVersion(Launcher::version());
    QGuiApplication::setWindowIcon(QIcon(":/powwow.ico"));

#ifdef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle("FluentWinUI3");
#endif

    QCommandLineParser parser;
    // after an empty line, which Qt does not put after the usage
    parser.setApplicationDescription(
        "\n" + QCoreApplication::translate("main", "The launcher of powwow."));
    parser.addHelpOption();
    // as Qt's, but with the copyright, as powwow --version shows it
    QCommandLineOption version({ "v", "version" },
                               QCoreApplication::translate(
                                   "main", "Displays version information."));
    parser.addOption(version);
    parser.addPositionalArgument(
        "definition-file",
        QCoreApplication::translate("main", "Connect to the MUD of the"
                                    " definition file in a terminal."),
        "[definition-file]");
    parser.process(app);
    // shown by Qt, which on Windows, without a console, does it in a
    // window, as the name and version of the program
    if (parser.isSet(version)) {
        QCoreApplication::setApplicationName("powwow-launcher");
        QCoreApplication::setApplicationVersion(
            Launcher::version() + ", " + (utf8 ? "©" : "Copyright")
            + " 1998" + (utf8 ? "–" : "-") + "2026 by the powwow authors\n"
            + "https://github.com/MUME/powwow");
        parser.showVersion();
    }

    // a definition file, such as one opened in the file manager: powwow
    // with it in a terminal
    const QStringList args = parser.positionalArguments();
    if (args.size() > 1)
        parser.showHelp(1);
    if (args.size() == 1)
        return Launcher().connectFile(args.at(0)) ? 0 : 1;

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); },
                     Qt::QueuedConnection);
    engine.loadFromModule("Powwow", "Main");
    return app.exec();
}
