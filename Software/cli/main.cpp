#include <QCoreApplication>
#include <QTextStream>

#include "clirunner.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("mdrewriter-cli"));
    QTextStream out(stdout);
    QTextStream err(stderr);
    CliRunner cli(out, err);
    return cli.run(a.arguments().mid(1));
}
