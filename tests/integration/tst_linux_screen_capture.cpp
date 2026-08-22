// SPDX-License-Identifier: GPL-3.0-only
#include "capture/QtScreenCaptureSource.h"
#include "selection/SelectionOverlayController.h"

#include <QGuiApplication>
#include <QIcon>
#include <QMediaCaptureSession>
#include <QPixmap>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QScreenCapture>
#include <QSystemTrayIcon>
#include <QTest>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWindow>

class LinuxScreenCaptureTest final : public QObject {
    Q_OBJECT

private slots:
    void hasXcbDisplayAndSystemTray();
    void selectionOverlayCoversWholeXcbScreen();
    void receivesARealDesktopFrame();
    void productionCaptureSourceForwardsARealDesktopFrame();
};

void LinuxScreenCaptureTest::hasXcbDisplayAndSystemTray() {
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("xcb"));
    QVERIFY2(QGuiApplication::primaryScreen() != nullptr,
             "Linux GUI validation requires a primary X11 screen");
    QTRY_VERIFY_WITH_TIMEOUT(QSystemTrayIcon::isSystemTrayAvailable(), 10000);

    QPixmap iconPixmap(16, 16);
    iconPixmap.fill(Qt::cyan);
    QSystemTrayIcon trayIcon{QIcon(iconPixmap)};
    trayIcon.show();
    QTRY_VERIFY_WITH_TIMEOUT(trayIcon.isVisible(), 3000);
    trayIcon.hide();
}

void LinuxScreenCaptureTest::selectionOverlayCoversWholeXcbScreen() {
    QScreen *const screen = QGuiApplication::primaryScreen();
    QVERIFY2(screen != nullptr, "Linux overlay validation requires a primary screen");

    cimbarpunk::SelectionOverlayController controller;
    controller.showForScreen(screen, std::nullopt);

    QWindow *overlay = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT([&overlay] {
        for (QWindow *window : QGuiApplication::topLevelWindows()) {
            if (window != nullptr && window->isVisible()
                && window->flags().testFlag(Qt::FramelessWindowHint)) {
                overlay = window;
                return true;
            }
        }
        return false;
    }(), 3000);
    QCOMPARE(overlay->geometry(), screen->geometry());
    QCOMPARE(overlay->mapToGlobal(QPoint(0, 0)), screen->geometry().topLeft());
    QCOMPARE(overlay->framePosition(), screen->geometry().topLeft());

    QProcess xwininfo;
    xwininfo.start(QStringLiteral("xwininfo"),
                   {QStringLiteral("-id"),
                    QStringLiteral("0x%1").arg(overlay->winId(), 0, 16)});
    QVERIFY(xwininfo.waitForStarted());
    QVERIFY(xwininfo.waitForFinished(3000));
    QCOMPARE(xwininfo.exitCode(), 0);
    const QString nativeGeometry = QString::fromUtf8(xwininfo.readAllStandardOutput());
    const QRegularExpression xExpression(
        QStringLiteral(R"(Absolute upper-left X:\s+(-?\d+))"));
    const QRegularExpression yExpression(
        QStringLiteral(R"(Absolute upper-left Y:\s+(-?\d+))"));
    const QRegularExpressionMatch xMatch = xExpression.match(nativeGeometry);
    const QRegularExpressionMatch yMatch = yExpression.match(nativeGeometry);
    QVERIFY(xMatch.hasMatch());
    QVERIFY(yMatch.hasMatch());
    QCOMPARE(QPoint(xMatch.captured(1).toInt(), yMatch.captured(1).toInt()),
             screen->geometry().topLeft());
}

void LinuxScreenCaptureTest::receivesARealDesktopFrame() {
    QScreen *const screen = QGuiApplication::primaryScreen();
    QVERIFY2(screen != nullptr, "Linux capture validation requires a primary screen");

    QScreenCapture capture;
    QVERIFY2(
        capture.error() != QScreenCapture::CapturingNotSupported,
        qPrintable(QStringLiteral("Qt screen capture backend is unavailable: %1").arg(capture.errorString()))
    );

    QMediaCaptureSession session;
    QVideoSink sink;
    int convertibleFrameCount = 0;
    connect(&sink, &QVideoSink::videoFrameChanged, this, [&convertibleFrameCount](const QVideoFrame &frame) {
        if (frame.isValid() && !frame.toImage().isNull()) {
            ++convertibleFrameCount;
        }
    });

    session.setScreenCapture(&capture);
    session.setVideoOutput(&sink);
    capture.setScreen(screen);
    capture.start();

    QTRY_VERIFY_WITH_TIMEOUT(capture.isActive(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(convertibleFrameCount > 0, 10000);

    capture.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!capture.isActive(), 5000);
}

void LinuxScreenCaptureTest::productionCaptureSourceForwardsARealDesktopFrame() {
    QScreen *const screen = QGuiApplication::primaryScreen();
    QVERIFY2(screen != nullptr, "Linux capture validation requires a primary screen");

    cimbarpunk::QtScreenCaptureSource source;
    int forwardedFrameCount = 0;
    int activeCount = 0;
    int inactiveCount = 0;
    QString failure;
    connect(&source, &cimbarpunk::ICaptureSource::frameReady, this,
            [&forwardedFrameCount](const QImage &frame) {
                if (!frame.isNull()) {
                    ++forwardedFrameCount;
                }
            });
    connect(&source, &cimbarpunk::ICaptureSource::activeChanged, this,
            [&activeCount, &inactiveCount](const bool active) {
                if (active) {
                    ++activeCount;
                } else if (activeCount > 0) {
                    ++inactiveCount;
                }
            });
    connect(&source, &cimbarpunk::ICaptureSource::failed, this,
            [&failure](const QString &message) { failure = message; });

    QString startError;
    QVERIFY2(source.start(screen, &startError), qPrintable(startError));
    QTRY_VERIFY_WITH_TIMEOUT(activeCount > 0 || !failure.isEmpty(), 5000);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QTRY_VERIFY_WITH_TIMEOUT(forwardedFrameCount > 0 || !failure.isEmpty(), 10000);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
    QVERIFY(forwardedFrameCount > 0);
    QCOMPARE(inactiveCount, 0);
    const int inactiveCountBeforeStop = inactiveCount;
    source.stop();
    QTRY_VERIFY_WITH_TIMEOUT(inactiveCount > inactiveCountBeforeStop, 5000);
}

QTEST_MAIN(LinuxScreenCaptureTest)

#include "tst_linux_screen_capture.moc"
