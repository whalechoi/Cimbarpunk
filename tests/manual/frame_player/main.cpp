// SPDX-License-Identifier: GPL-3.0-only
#include "FramePlayer.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Cimbarpunk Test Frame Player"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Loops committed cimbar fixture frames at 10 FPS."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("fixture-directory"),
                                 QStringLiteral("Directory containing manifest.json and mode folders."),
                                 QStringLiteral("[fixture-directory]"));
    parser.process(application);

    const QStringList arguments = parser.positionalArguments();
    if (arguments.size() > 1) {
        parser.showHelp(2);
    }
    const QString fixtureDirectory = arguments.isEmpty()
        ? QStringLiteral(CIMBARPUNK_DEFAULT_FIXTURE_DIR)
        : arguments.constFirst();

    QString error;
    FramePlayer player(fixtureDirectory, &error);
    if (!player.isReady()) {
        qCritical().noquote() << error;
        return 2;
    }
    player.show();
    return application.exec();
}
