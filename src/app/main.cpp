// SPDX-License-Identifier: GPL-3.0-only
#include "app/AppRuntime.h"
#include "core/Version.h"

#include <QApplication>
#include <QMessageBox>
#include <QString>
#include <QSystemTrayIcon>

#include <cstdlib>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("Cimbarpunk"));
    application.setOrganizationName(QStringLiteral("Cimbarpunk"));
    application.setApplicationVersion(
        QString::fromLatin1(cimbarpunk::versionString().data()));
    application.setQuitOnLastWindowClosed(false);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::critical(nullptr, QStringLiteral("Cimbarpunk"),
            QStringLiteral("当前桌面环境不支持系统托盘。"));
        return EXIT_FAILURE;
    }

    cimbarpunk::AppRuntime runtime;
    runtime.start();
    return application.exec();
}
