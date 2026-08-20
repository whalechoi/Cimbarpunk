// SPDX-License-Identifier: GPL-3.0-only
#include "capture/QtScreenCaptureSource.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaCaptureSession>
#include <QScreen>
#include <QScreenCapture>
#include <QTest>
#include <QVideoFrame>
#include <QVideoSink>
#include <qt_windows.h>

#include <array>

class WindowsScreenCaptureTest final : public QObject {
    Q_OBJECT

private slots:
    void receivesARealDesktopFrame();
    void productionCaptureSourceForwardsARealDesktopFrame();
};

void WindowsScreenCaptureTest::receivesARealDesktopFrame() {
    QScreen *const screen = QGuiApplication::primaryScreen();
    QVERIFY2(screen != nullptr, "Windows capture validation requires a primary screen");

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

    const QString expectedPlugin = qEnvironmentVariable("CIMBARPUNK_EXPECT_FFMPEG_PLUGIN");
    if (!expectedPlugin.isEmpty()) {
        const HMODULE pluginModule = GetModuleHandleW(L"ffmpegmediaplugin.dll");
        QVERIFY2(pluginModule != nullptr, "The real capture did not load ffmpegmediaplugin.dll");

        std::array<wchar_t, 32768> modulePath{};
        const DWORD modulePathLength =
            GetModuleFileNameW(pluginModule, modulePath.data(), static_cast<DWORD>(modulePath.size()));
        QVERIFY2(modulePathLength > 0 && modulePathLength < modulePath.size(),
                 "Could not resolve the loaded FFmpeg multimedia plugin path");

        const QString actualPlugin =
            QFileInfo(QString::fromWCharArray(modulePath.data(), static_cast<qsizetype>(modulePathLength)))
                .canonicalFilePath();
        const QString canonicalExpected = QFileInfo(expectedPlugin).canonicalFilePath();
        QVERIFY2(!canonicalExpected.isEmpty(), "Expected staged FFmpeg multimedia plugin is missing");
        QVERIFY2(QString::compare(actualPlugin, canonicalExpected, Qt::CaseInsensitive) == 0,
                 qPrintable(QStringLiteral("Capture loaded FFmpeg plugin outside the staged package: %1")
                                .arg(actualPlugin)));
    }

    capture.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!capture.isActive(), 5000);
}

void WindowsScreenCaptureTest::productionCaptureSourceForwardsARealDesktopFrame() {
    QScreen *const screen = QGuiApplication::primaryScreen();
    QVERIFY2(screen != nullptr, "Windows capture validation requires a primary screen");

    cimbarpunk::QtScreenCaptureSource source;
    int forwardedFrameCount = 0;
    int activeCount = 0;
    QString failure;
    connect(&source, &cimbarpunk::ICaptureSource::frameReady, this,
            [&forwardedFrameCount](const QImage &frame) {
                if (!frame.isNull()) {
                    ++forwardedFrameCount;
                }
            });
    connect(&source, &cimbarpunk::ICaptureSource::activeChanged, this,
            [&activeCount](const bool active) {
                if (active) {
                    ++activeCount;
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

    source.stop();
}

QTEST_MAIN(WindowsScreenCaptureTest)

#include "tst_windows_screen_capture.moc"
