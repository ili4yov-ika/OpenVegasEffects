// The OpenFX sandbox for the tests: what the application does when it is run
// with --ofx-describe <binary> <out.json>, without the application.

#include "plugin/OfxHost.h"

#include <QCoreApplication>
#include <QStringList>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const qsizetype at = args.indexOf(QStringLiteral("--ofx-describe"));
    if (at < 0 || at + 2 >= args.size()) return 1;
    return openvegas::plugin::runOfxDescribeSandbox(args.at(at + 1), args.at(at + 2));
}
