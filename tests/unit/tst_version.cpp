// SPDX-License-Identifier: GPL-3.0-only
#include "core/Version.h"

#include <QtTest/QTest>

class VersionTest final : public QObject {
    Q_OBJECT

private slots:
    void exposesPinnedApplicationVersion() {
        QCOMPARE(cimbarpunk::versionString(), std::string_view{"0.1.0"});
    }
};

QTEST_GUILESS_MAIN(VersionTest)
#include "tst_version.moc"
