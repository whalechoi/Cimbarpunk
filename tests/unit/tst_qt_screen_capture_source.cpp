// SPDX-License-Identifier: GPL-3.0-only
#include "capture/QtScreenCaptureSource.h"

#include <QImage>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

#include <chrono>

namespace {

class FakeScreenCaptureBackend final : public cimbarpunk::detail::IScreenCaptureBackend {
public:
    void setCallbacks(Callbacks newCallbacks) override {
        callbacks = std::move(newCallbacks);
        if (callbacks.frameReady || callbacks.activeChanged || callbacks.failed) {
            retainedCallbacks.append(callbacks);
        }
    }

    void setScreen(QScreen* screen) override {
        selectedScreen = screen;
        events.append(QStringLiteral("screen"));
    }

    void start() override {
        events.append(QStringLiteral("start.begin"));
        if (emitErrorOnStart && callbacks.failed) {
            callbacks.failed(errorOnStart);
        }
        if (reactivateAfterErrorOnStart) {
            backendActive = true;
            events.append(QStringLiteral("start.reactivated"));
        }
        if (emitActiveOnStart && callbacks.activeChanged) {
            backendActive = true;
            callbacks.activeChanged(true);
            callbacks.activeChanged(true);
        }
        events.append(QStringLiteral("start.end"));
    }

    void stop() override {
        events.append(QStringLiteral("stop.begin"));
        backendActive = false;
        if (emitActiveOnStop && callbacks.activeChanged) {
            callbacks.activeChanged(false);
            callbacks.activeChanged(false);
        }
        events.append(QStringLiteral("stop.end"));
    }

    void sendFrame(const QImage& frame) {
        if (callbacks.frameReady) {
            callbacks.frameReady(frame);
        }
    }

    void sendError(const QString& message) {
        if (callbacks.failed) {
            callbacks.failed(message);
        }
    }

    void sendActiveFromAttempt(const qsizetype attempt, const bool active) {
        if (retainedCallbacks.at(attempt).activeChanged) {
            retainedCallbacks.at(attempt).activeChanged(active);
        }
    }

    void sendErrorFromAttempt(const qsizetype attempt, const QString& message) {
        if (retainedCallbacks.at(attempt).failed) {
            retainedCallbacks.at(attempt).failed(message);
        }
    }

    Callbacks callbacks;
    QList<Callbacks> retainedCallbacks;
    QStringList events;
    QScreen* selectedScreen = nullptr;
    bool emitActiveOnStart = false;
    bool emitActiveOnStop = false;
    bool emitErrorOnStart = false;
    bool reactivateAfterErrorOnStart = false;
    bool backendActive = false;
    QString errorOnStart;
};

QScreen* testScreen() {
    return QGuiApplication::primaryScreen();
}

} // namespace

class QtScreenCaptureSourceTest final : public QObject {
    Q_OBJECT

private slots:
    void rejectsNullScreen() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend);
        QString error;

        QVERIFY(!source->start(nullptr, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(backend->events.isEmpty());
    }

    void startsAndStopsBackendInOrder() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend);

        QVERIFY(source->start(testScreen(), nullptr));
        QVERIFY(backend->selectedScreen == testScreen());
        QCOMPARE(backend->events,
            QStringList({QStringLiteral("screen"), QStringLiteral("start.begin"),
                QStringLiteral("start.end")}));

        source->stop();
        source->stop();
        QCOMPARE(backend->events,
            QStringList({QStringLiteral("screen"), QStringLiteral("start.begin"),
                QStringLiteral("start.end"), QStringLiteral("stop.begin"),
                QStringLiteral("stop.end")}));
    }

    void rejectsSecondStart() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend);
        QVERIFY(source->start(testScreen(), nullptr));

        QString error;
        QVERIFY(!source->start(testScreen(), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(backend->events.count(QStringLiteral("start.begin")), 1);
    }

    void propagatesOnlyRealActiveTransitions() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend);
        backend->emitActiveOnStart = true;
        backend->emitActiveOnStop = true;
        QSignalSpy active(source.get(), &cimbarpunk::ICaptureSource::activeChanged);

        QVERIFY(source->start(testScreen(), nullptr));
        source->stop();

        QCOMPARE(active.count(), 2);
        QCOMPARE(active.at(0).at(0).toBool(), true);
        QCOMPARE(active.at(1).at(0).toBool(), false);
    }

    void copiesValidFramesAndRejectsInvalidFrames() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend);
        QSignalSpy frames(source.get(), &cimbarpunk::ICaptureSource::frameReady);
        QVERIFY(source->start(testScreen(), nullptr));

        backend->sendFrame({});
        QCOMPARE(frames.count(), 0);

        QImage producerFrame(2, 1, QImage::Format_RGBA8888);
        producerFrame.fill(Qt::red);
        backend->sendFrame(producerFrame);
        QCOMPARE(frames.count(), 1);
        const QImage received = qvariant_cast<QImage>(frames.at(0).at(0));
        producerFrame.fill(Qt::blue);
        QCOMPARE(received.pixelColor(0, 0), QColor(Qt::red));

        backend->sendFrame(producerFrame);
        QCOMPARE(frames.count(), 2);
        source->stop();
        backend->sendFrame(producerFrame);
        QCOMPARE(frames.count(), 2);
    }

    void propagatesBackendErrorOnceAndStops() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend);
        QSignalSpy failures(source.get(), &cimbarpunk::ICaptureSource::failed);
        QVERIFY(source->start(testScreen(), nullptr));

        backend->sendError(QStringLiteral("backend failed"));
        backend->sendError(QStringLiteral("duplicate"));

        QCOMPARE(failures.count(), 1);
        QCOMPARE(failures.at(0).at(0).toString(), QStringLiteral("backend failed"));
        QCOMPARE(backend->events.count(QStringLiteral("stop.begin")), 1);
    }

    void returnsSynchronousBackendStartError() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend);
        backend->emitErrorOnStart = true;
        backend->reactivateAfterErrorOnStart = true;
        backend->errorOnStart = QStringLiteral("synchronous failure");
        QSignalSpy failures(source.get(), &cimbarpunk::ICaptureSource::failed);
        QString error;

        QVERIFY(!source->start(testScreen(), &error));
        QVERIFY(!backend->backendActive);
        QCOMPARE(error, QStringLiteral("synchronous failure"));
        QCOMPARE(failures.count(), 1);
        QCOMPARE(backend->events,
            QStringList({QStringLiteral("screen"), QStringLiteral("start.begin"),
                QStringLiteral("stop.begin"), QStringLiteral("stop.end"),
                QStringLiteral("start.reactivated"), QStringLiteral("start.end"),
                QStringLiteral("stop.begin"), QStringLiteral("stop.end")}));
    }

    void rejectsReentrantStartFromSynchronousFailure() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend);
        backend->emitErrorOnStart = true;
        backend->errorOnStart = QStringLiteral("synchronous failure");
        bool restarted = true;
        QString restartError;
        connect(source.get(), &cimbarpunk::ICaptureSource::failed, source.get(), [&] {
            backend->emitErrorOnStart = false;
            restarted = source->start(testScreen(), &restartError);
        });

        QString error;
        QVERIFY(!source->start(testScreen(), &error));

        QVERIFY(!restarted);
        QVERIFY(!restartError.isEmpty());
        QCOMPARE(error, QStringLiteral("synchronous failure"));
        QCOMPARE(backend->events.count(QStringLiteral("start.begin")), 1);
    }

    void failsIfBackendNeverBecomesActive() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend, std::chrono::milliseconds(10));
        QSignalSpy failures(source.get(), &cimbarpunk::ICaptureSource::failed);
        QSignalSpy active(source.get(), &cimbarpunk::ICaptureSource::activeChanged);

        QVERIFY(source->start(testScreen(), nullptr));
        QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 200);

        QCOMPARE(failures.at(0).at(0).toString(), QStringLiteral("屏幕捕获未能启动"));
        QCOMPARE(active.count(), 0);
        QCOMPARE(backend->events.count(QStringLiteral("stop.begin")), 1);
        QVERIFY(!backend->backendActive);

        backend->sendActiveFromAttempt(0, true);
        QCoreApplication::processEvents();
        QCOMPARE(failures.count(), 1);
        QCOMPARE(active.count(), 0);
        QCOMPARE(backend->events.count(QStringLiteral("stop.begin")), 1);
        QVERIFY(!backend->backendActive);
    }

    void oldStartupTimeoutCannotFailRestartedCaptureEarly() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend, std::chrono::milliseconds(120));
        QSignalSpy failures(source.get(), &cimbarpunk::ICaptureSource::failed);

        QVERIFY(source->start(testScreen(), nullptr));
        QTest::qWait(40);
        source->stop();
        QVERIFY(source->start(testScreen(), nullptr));

        QTest::qWait(90);
        QCOMPARE(failures.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 200);
        QCOMPARE(failures.at(0).at(0).toString(), QStringLiteral("屏幕捕获未能启动"));
        QCOMPARE(backend->events.count(QStringLiteral("stop.begin")), 2);
    }

    void staleAttemptEventsCannotAffectReentrantRestart() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend, std::chrono::milliseconds(20));
        bool restarted = false;
        int restartAttempts = 0;
        connect(source.get(), &cimbarpunk::ICaptureSource::failed, source.get(),
            [&](const QString&) {
                if (restartAttempts++ == 0) {
                    restarted = source->start(testScreen(), nullptr);
                }
            });
        QSignalSpy failures(source.get(), &cimbarpunk::ICaptureSource::failed);
        QSignalSpy active(source.get(), &cimbarpunk::ICaptureSource::activeChanged);

        QVERIFY(source->start(testScreen(), nullptr));
        QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 1, 200);

        QVERIFY(restarted);
        QCOMPARE(failures.at(0).at(0).toString(), QStringLiteral("屏幕捕获未能启动"));
        QCOMPARE(backend->retainedCallbacks.size(), 2);
        QCOMPARE(backend->events.count(QStringLiteral("stop.begin")), 1);

        backend->sendActiveFromAttempt(0, true);
        backend->sendErrorFromAttempt(0, QStringLiteral("stale A failure"));
        QCoreApplication::processEvents();

        QCOMPARE(active.count(), 0);
        QCOMPARE(failures.count(), 1);
        QCOMPARE(backend->events.count(QStringLiteral("stop.begin")), 1);

        QTRY_COMPARE_WITH_TIMEOUT(failures.count(), 2, 200);
        QCOMPARE(failures.at(1).at(0).toString(), QStringLiteral("屏幕捕获未能启动"));
        QCOMPARE(backend->events.count(QStringLiteral("stop.begin")), 2);
    }

    void ignoresDisplaySignalsAfterStop() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend);
        QSignalSpy failures(source.get(), &cimbarpunk::ICaptureSource::failed);
        QVERIFY(source->start(testScreen(), nullptr));
        source->stop();

        QVERIFY(!QObject::disconnect(testScreen(), nullptr, source.get(), nullptr));
        QVERIFY(QMetaObject::invokeMethod(testScreen(), "geometryChanged", Qt::DirectConnection,
            Q_ARG(QRect, testScreen()->geometry())));

        QCOMPARE(failures.count(), 0);
        QCOMPARE(backend->events.count(QStringLiteral("stop.begin")), 1);
    }

    void abortsOnceWhenDisplayChanges() {
        FakeScreenCaptureBackend* backend = nullptr;
        auto source = createSource(backend);
        backend->emitActiveOnStart = true;
        backend->emitActiveOnStop = true;
        QSignalSpy failures(source.get(), &cimbarpunk::ICaptureSource::failed);
        QSignalSpy active(source.get(), &cimbarpunk::ICaptureSource::activeChanged);
        QVERIFY(source->start(testScreen(), nullptr));

        QVERIFY(QMetaObject::invokeMethod(testScreen(), "geometryChanged", Qt::DirectConnection,
            Q_ARG(QRect, testScreen()->geometry())));
        QVERIFY(QMetaObject::invokeMethod(testScreen(), "logicalDotsPerInchChanged",
            Qt::DirectConnection, Q_ARG(qreal, testScreen()->logicalDotsPerInch())));

        QCOMPARE(failures.count(), 1);
        QCOMPARE(failures.at(0).at(0).toString(),
            QStringLiteral("显示器配置在捕获期间发生变化，请重新选择区域"));
        QCOMPARE(backend->events.count(QStringLiteral("stop.begin")), 1);
        QCOMPARE(active.count(), 2);
        QCOMPARE(active.at(0).at(0).toBool(), true);
        QCOMPARE(active.at(1).at(0).toBool(), false);
    }

    void abortsOnEachQtScreenSignal() {
        QScreen* screen = testScreen();
        QVERIFY(screen != nullptr);

        const auto verifySignal = [screen](const auto& emitSignal) {
            FakeScreenCaptureBackend* backend = nullptr;
            auto source = createSource(backend);
            QSignalSpy failures(source.get(), &cimbarpunk::ICaptureSource::failed);
            QVERIFY(source->start(screen, nullptr));

            QVERIFY(emitSignal());

            QCOMPARE(failures.count(), 1);
            QCOMPARE(failures.at(0).at(0).toString(),
                QStringLiteral("显示器配置在捕获期间发生变化，请重新选择区域"));
            QCOMPARE(backend->events.count(QStringLiteral("stop.begin")), 1);
        };

        verifySignal([screen] {
            return QMetaObject::invokeMethod(screen, "geometryChanged", Qt::DirectConnection,
                Q_ARG(QRect, screen->geometry()));
        });
        verifySignal([screen] {
            return QMetaObject::invokeMethod(screen, "logicalDotsPerInchChanged",
                Qt::DirectConnection, Q_ARG(qreal, screen->logicalDotsPerInch()));
        });
        verifySignal([screen] {
            return QMetaObject::invokeMethod(screen, "physicalDotsPerInchChanged",
                Qt::DirectConnection, Q_ARG(qreal, screen->physicalDotsPerInch()));
        });
        verifySignal([screen] {
            return QMetaObject::invokeMethod(screen, "orientationChanged", Qt::DirectConnection,
                Q_ARG(Qt::ScreenOrientation, screen->orientation()));
        });
    }

private:
    static std::unique_ptr<cimbarpunk::QtScreenCaptureSource> createSource(
        FakeScreenCaptureBackend*& fake,
        const std::chrono::milliseconds startupTimeout = std::chrono::seconds(10)) {
        auto backend = std::make_unique<FakeScreenCaptureBackend>();
        fake = backend.get();
        return std::unique_ptr<cimbarpunk::QtScreenCaptureSource>(
            new cimbarpunk::QtScreenCaptureSource(
                std::move(backend), startupTimeout));
    }
};

QTEST_MAIN(QtScreenCaptureSourceTest)

#include "tst_qt_screen_capture_source.moc"
