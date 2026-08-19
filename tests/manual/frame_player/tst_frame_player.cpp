// SPDX-License-Identifier: GPL-3.0-only
#include "FramePlayer.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>

namespace {

void writeManifest(const QString &directory, const QByteArray &json)
{
    QFile file(QDir(directory).filePath(QStringLiteral("manifest.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(json), json.size());
}

void writeFrame(const QString &directory, const QString &name, const QColor &colour)
{
    QDir dir(directory);
    QVERIFY(dir.mkpath(QStringLiteral("mode68")));
    QImage image(12, 8, QImage::Format_RGB32);
    image.fill(colour);
    QVERIFY(image.save(dir.filePath(QStringLiteral("mode68/") + name)));
}

QColor displayedCentreColour(const FramePlayer &player)
{
    const auto *label = player.findChild<QLabel *>(QStringLiteral("frameDisplay"));
    Q_ASSERT(label != nullptr);
    const QPixmap pixmap = label->pixmap(Qt::ReturnByValue);
    return pixmap.toImage().pixelColor(pixmap.width() / 2, pixmap.height() / 2);
}

} // namespace

class FramePlayerTest : public QObject
{
    Q_OBJECT

private slots:
    void manifestOrderPlaysAtTenFpsAndLoops();
    void windowUsesBlackBackgroundAndCentresFrames();
    void escapeClosesTheWindow();
    void invalidManifestIsRejected();
};

void FramePlayerTest::manifestOrderPlaysAtTenFpsAndLoops()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    writeFrame(fixture.path(), QStringLiteral("second.png"), Qt::green);
    writeFrame(fixture.path(), QStringLiteral("first.png"), Qt::red);
    writeManifest(fixture.path(), R"({"mode":68,"orderedFrames":["first.png","second.png"]})");

    QString error;
    FramePlayer player(fixture.path(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(player.windowTitle(), QStringLiteral("Cimbarpunk Test Frame Player"));
    QCOMPARE(displayedCentreColour(player), QColor(Qt::red));

    QTRY_COMPARE_WITH_TIMEOUT(displayedCentreColour(player), QColor(Qt::green), 180);
    QTRY_COMPARE_WITH_TIMEOUT(displayedCentreColour(player), QColor(Qt::red), 180);
}

void FramePlayerTest::windowUsesBlackBackgroundAndCentresFrames()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    writeFrame(fixture.path(), QStringLiteral("frame.png"), Qt::blue);
    writeManifest(fixture.path(), R"({"mode":68,"orderedFrames":["frame.png"]})");

    QString error;
    FramePlayer player(fixture.path(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(player.palette().color(QPalette::Window), QColor(Qt::black));

    const auto *label = player.findChild<QLabel *>(QStringLiteral("frameDisplay"));
    QVERIFY(label != nullptr);
    QCOMPARE(label->alignment(), Qt::AlignCenter);
}

void FramePlayerTest::escapeClosesTheWindow()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    writeFrame(fixture.path(), QStringLiteral("frame.png"), Qt::blue);
    writeManifest(fixture.path(), R"({"mode":68,"orderedFrames":["frame.png"]})");

    QString error;
    FramePlayer player(fixture.path(), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    player.show();
    QVERIFY(QTest::qWaitForWindowExposed(&player));
    QTest::keyClick(&player, Qt::Key_Escape);
    QTRY_VERIFY(!player.isVisible());
}

void FramePlayerTest::invalidManifestIsRejected()
{
    QTemporaryDir fixture;
    QVERIFY(fixture.isValid());
    writeManifest(fixture.path(), R"({"mode":68,"orderedFrames":[]})");

    QString error;
    FramePlayer player(fixture.path(), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(!player.isReady());
}

QTEST_MAIN(FramePlayerTest)
#include "tst_frame_player.moc"
