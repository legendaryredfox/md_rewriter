#include "mainwindow.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("EpicJoy"));
    QCoreApplication::setApplicationName(QStringLiteral("MDRewriter"));

    // Translations are compiled into the executable (see MDRewriter.pro); English is the source language.
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        a.installTranslator(&qtTranslator);
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("MDRewriter"), QStringLiteral("_"), QStringLiteral(":/i18n")))
        a.installTranslator(&translator);

    MainWindow w;
    w.show();
    return a.exec();
}
