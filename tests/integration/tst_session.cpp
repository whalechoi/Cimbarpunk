// SPDX-License-Identifier: GPL-3.0-only
#include "fakes/FakeCaptureSource.h"
#include "fakes/FakeFrameProcessor.h"
#include "session/CaptureSession.h"
#include "settings/SettingsStore.h"

#include <QEventLoop>
#include <QImage>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QTest>

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

using namespace std::chrono_literals;

namespace {

constexpr int idle = static_cast<int>(cimbarpunk::SessionState::Idle);
constexpr int selecting = static_cast<int>(cimbarpunk::SessionState::Selecting);
constexpr int adjusting = static_cast<int>(cimbarpunk::SessionState::Adjusting);
constexpr int capturing = static_cast<int>(cimbarpunk::SessionState::Capturing);
constexpr int completed = static_cast<int>(cimbarpunk::SessionState::Completed);
constexpr int error = static_cast<int>(cimbarpunk::SessionState::Error);
constexpr int cancelled = static_cast<int>(cimbarpunk::SessionState::Cancelled);

cimbarpunk::ScreenSelection selection(const QString& screenId = QStringLiteral("display-A")) {
    return {
        .screenId = screenId,
        .screenGeometry = QRectF(-1920.0, 0.0, 1920.0, 1080.0),
        .logicalRect = QRectF(-1728.0, 108.0, 960.0, 540.0),
    };
}

QScreen* screenSentinel(const quintptr value = 0x1) {
    return reinterpret_cast<QScreen*>(value);
}

QList<int> recordedStates(const QSignalSpy& spy) {
    QList<int> states;
    states.reserve(spy.size());
    for (const QList<QVariant>& arguments : spy) {
        states.append(static_cast<int>(arguments.at(0).value<cimbarpunk::SessionState>()));
    }
    return states;
}

int occurrences(const QList<int>& values, int wanted) {
    return static_cast<int>(std::count(values.cbegin(), values.cend(), wanted));
}

bool processQueuedReturnToIdle(cimbarpunk::CaptureSession& session) {
    if (session.state() == cimbarpunk::SessionState::Idle) {
        return true;
    }

    QEventLoop loop;
    QTimer bound;
    bound.setSingleShot(true);
    bound.setInterval(500ms);
    const QMetaObject::Connection stateConnection = QObject::connect(
        &session, &cimbarpunk::CaptureSession::stateChanged, &loop,
        [&loop](const cimbarpunk::SessionState state) {
            if (state == cimbarpunk::SessionState::Idle) {
                loop.quit();
            }
        });
    QObject::connect(&bound, &QTimer::timeout, &loop, &QEventLoop::quit);
    bound.start();
    loop.exec();
    QObject::disconnect(stateConnection);
    return session.state() == cimbarpunk::SessionState::Idle;
}

} // namespace

class CaptureSessionTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<cimbarpunk::SessionState>();
        qRegisterMetaType<cimbarpunk::OutputResult>();
    }

    void successfulSequenceSnapshotsInputsAndPersistsSelection() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        settings.setOutputDirectory(QStringLiteral("first-output"));

        QStringList events;
        cimbarpunk::test::FakeCaptureSource source(&events);
        cimbarpunk::test::FakeFrameProcessor processor(&events);
        QString resolvedId;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [&resolvedId](QStringView screenId) {
                resolvedId = screenId.toString();
                return screenSentinel();
            });
        QSignalSpy states(&session, &cimbarpunk::CaptureSession::stateChanged);
        QSignalSpy selectionRequests(&session, &cimbarpunk::CaptureSession::selectionRequested);
        QSignalSpy successes(&session, &cimbarpunk::CaptureSession::completed);
        bool stoppedBeforeSuccess = false;
        connect(&session, &cimbarpunk::CaptureSession::completed, &session,
            [&] { stoppedBeforeSuccess = !source.active && !processor.running; });

        QVERIFY(session.beginSelection());
        QVERIFY(!session.beginSelection());
        QCOMPARE(selectionRequests.size(), 1);
        QVERIFY(session.selectionCreated(selection()));
        QVERIFY(!session.beginSelection());
        QVERIFY(!settings.restoreSelection(QStringLiteral("display-A")).has_value());

        QVERIFY(session.confirmSelection());
        QCOMPARE(resolvedId, QStringLiteral("display-A"));
        QCOMPARE(processor.startCalls, 1);
        QCOMPARE(source.startCalls, 1);
        QCOMPARE(source.lastScreen, screenSentinel());
        QCOMPARE(events.first(2), QStringList({QStringLiteral("processor.start"), QStringLiteral("capture.start")}));
        QCOMPARE(processor.lastOutputDirectory, QStringLiteral("first-output"));
        QVERIFY(!session.beginSelection());
        QVERIFY(!session.confirmSelection());
        QCOMPARE(source.startCalls, 1);

        settings.setOutputDirectory(QStringLiteral("second-output"));
        source.sendFrame(QImage(4, 4, QImage::Format_RGB888));
        QCOMPARE(processor.submittedFrames.size(), 1);

        processor.succeed({.ok = true, .finalPath = QStringLiteral("decoded.bin")});
        QCOMPARE(session.state(), cimbarpunk::SessionState::Completed);
        QVERIFY(!session.beginSelection());
        QCOMPARE(successes.size(), 1);
        QVERIFY(stoppedBeforeSuccess);
        QVERIFY(!source.active);
        QVERIFY(!processor.running);

        const auto persisted = settings.restoreSelection(QStringLiteral("display-A"));
        QVERIFY(persisted.has_value());
        QCOMPARE(*persisted, QRectF(0.1, 0.1, 0.5, 0.5));

        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
        const QList<int> expected{selecting, adjusting, capturing, completed, idle};
        QCOMPARE(recordedStates(states), expected);
        QCOMPARE(occurrences(recordedStates(states), idle), 1);

        source.sendFrame(QImage(8, 8, QImage::Format_RGB888));
        processor.succeed({.ok = true, .finalPath = QStringLiteral("late.bin")});
        processor.reportFailure(QStringLiteral("late failure"));
        QCOMPARE(processor.submittedFrames.size(), 1);
        QCOMPARE(successes.size(), 1);
    }

    void selectingStateHandlerCanConfirmWithoutLosingTheSelection() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [](QStringView) { return screenSentinel(); });
        bool selectionAccepted = false;
        bool selectionConfirmed = false;
        connect(&session, &cimbarpunk::CaptureSession::stateChanged, &session,
            [&](const cimbarpunk::SessionState state) {
                if (state == cimbarpunk::SessionState::Selecting) {
                    selectionAccepted = session.selectionCreated(selection());
                    selectionConfirmed = session.confirmSelection();
                }
            });

        QVERIFY(session.beginSelection());
        QVERIFY(selectionAccepted);
        QVERIFY(selectionConfirmed);
        QCOMPARE(session.state(), cimbarpunk::SessionState::Capturing);
        processor.succeed({.ok = true, .finalPath = QStringLiteral("decoded.bin")});

        const auto persisted = settings.restoreSelection(QStringLiteral("display-A"));
        QVERIFY(persisted.has_value());
        QCOMPARE(*persisted, QRectF(0.1, 0.1, 0.5, 0.5));
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
    }

    void staleConfirmAfterCapturingNotificationCannotStartOrFailTheNewSession() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        QScreen* const screenA = screenSentinel(0xA);
        QScreen* const screenB = screenSentinel(0xB);
        source.failingScreen = screenA;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [=](const QStringView id) {
                return id == QStringLiteral("display-A") ? screenA : screenB;
            });
        QSignalSpy failures(&session, &cimbarpunk::CaptureSession::failed);
        bool handledA = false;
        bool reachedNestedIdle = false;
        bool beganB = false;
        bool selectedB = false;
        bool confirmedB = false;
        connect(&session, &cimbarpunk::CaptureSession::stateChanged, &session,
            [&](const cimbarpunk::SessionState state) {
                if (state != cimbarpunk::SessionState::Capturing || handledA) {
                    return;
                }
                handledA = true;
                session.stop();
                reachedNestedIdle = processQueuedReturnToIdle(session);
                if (!reachedNestedIdle) {
                    return;
                }
                beganB = session.beginSelection();
                selectedB = session.selectionCreated(selection(QStringLiteral("display-B")));
                confirmedB = session.confirmSelection();
            });

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        const bool confirmedA = session.confirmSelection();

        QVERIFY(!confirmedA);
        QVERIFY(reachedNestedIdle);
        QVERIFY(beganB);
        QVERIFY(selectedB);
        QVERIFY(confirmedB);
        QCOMPARE(session.state(), cimbarpunk::SessionState::Capturing);
        QCOMPARE(failures.size(), 0);
        QCOMPARE(source.startedScreens, QList<QScreen*>({screenB}));
        QCOMPARE(processor.lastSelection.screenId, QStringLiteral("display-B"));
        QVERIFY(source.active);
        QVERIFY(processor.running);
        QVERIFY(session.stop());
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
    }

    void staleConfirmAfterResolverCannotConsumeANewAdjustingSelection() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        QScreen* const screenA = screenSentinel(0xA);
        QScreen* const screenB = screenSentinel(0xB);
        cimbarpunk::CaptureSession* sessionPointer = nullptr;
        bool handledA = false;
        bool reachedNestedIdle = false;
        bool beganB = false;
        bool selectedB = false;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [&](const QStringView id) {
                if (id == QStringLiteral("display-A") && !handledA) {
                    handledA = true;
                    sessionPointer->cancel();
                    reachedNestedIdle = processQueuedReturnToIdle(*sessionPointer);
                    if (reachedNestedIdle) {
                        beganB = sessionPointer->beginSelection();
                        selectedB = sessionPointer->selectionCreated(
                            selection(QStringLiteral("display-B")));
                    }
                    return screenA;
                }
                return screenB;
            });
        sessionPointer = &session;

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        QVERIFY(!session.confirmSelection());

        QVERIFY(reachedNestedIdle);
        QVERIFY(beganB);
        QVERIFY(selectedB);
        QCOMPARE(session.state(), cimbarpunk::SessionState::Adjusting);
        QCOMPARE(processor.startCalls, 0);
        QCOMPARE(source.startCalls, 0);

        QVERIFY(session.confirmSelection());
        QCOMPARE(source.startedScreens, QList<QScreen*>({screenB}));
        QCOMPARE(processor.lastSelection.screenId, QStringLiteral("display-B"));
        QVERIFY(session.stop());
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
    }

    void staleConfirmAfterProcessorStartCannotDisconnectOrStopTheNewCapture() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        QScreen* const screenA = screenSentinel(0xA);
        QScreen* const screenB = screenSentinel(0xB);
        cimbarpunk::CaptureSession session(source, processor, settings,
            [=](const QStringView id) {
                return id == QStringLiteral("display-A") ? screenA : screenB;
            });
        bool handledA = false;
        bool reachedNestedIdle = false;
        bool beganB = false;
        bool selectedB = false;
        bool confirmedB = false;
        processor.onStarted = [&](const cimbarpunk::ScreenSelection& startedSelection) {
            if (startedSelection.screenId != QStringLiteral("display-A") || handledA) {
                return;
            }
            handledA = true;
            session.cancel();
            reachedNestedIdle = processQueuedReturnToIdle(session);
            if (!reachedNestedIdle) {
                return;
            }
            beganB = session.beginSelection();
            selectedB = session.selectionCreated(selection(QStringLiteral("display-B")));
            confirmedB = session.confirmSelection();
        };

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        QVERIFY(!session.confirmSelection());

        QVERIFY(reachedNestedIdle);
        QVERIFY(beganB);
        QVERIFY(selectedB);
        QVERIFY(confirmedB);
        QCOMPARE(session.state(), cimbarpunk::SessionState::Capturing);
        QCOMPARE(source.startedScreens, QList<QScreen*>({screenB}));
        QCOMPARE(processor.lastSelection.screenId, QStringLiteral("display-B"));
        QVERIFY(source.active);
        QVERIFY(processor.running);
        QVERIFY(session.stop());
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
    }

    void staleCaptureStartFailureCannotFailTheNewCapture() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        QScreen* const screenA = screenSentinel(0xA);
        QScreen* const screenB = screenSentinel(0xB);
        source.failingScreen = screenA;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [=](const QStringView id) {
                return id == QStringLiteral("display-A") ? screenA : screenB;
            });
        QSignalSpy failures(&session, &cimbarpunk::CaptureSession::failed);
        bool handledA = false;
        bool reachedNestedIdle = false;
        bool beganB = false;
        bool selectedB = false;
        bool confirmedB = false;
        source.onStarted = [&](QScreen* screen) {
            if (screen != screenA || handledA) {
                return;
            }
            handledA = true;
            session.stop();
            reachedNestedIdle = processQueuedReturnToIdle(session);
            if (!reachedNestedIdle) {
                return;
            }
            beganB = session.beginSelection();
            selectedB = session.selectionCreated(selection(QStringLiteral("display-B")));
            confirmedB = session.confirmSelection();
        };

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        QVERIFY(!session.confirmSelection());

        QVERIFY(reachedNestedIdle);
        QVERIFY(beganB);
        QVERIFY(selectedB);
        QVERIFY(confirmedB);
        QCOMPARE(session.state(), cimbarpunk::SessionState::Capturing);
        QCOMPARE(failures.size(), 0);
        QCOMPARE(source.startedScreens, QList<QScreen*>({screenA, screenB}));
        QCOMPARE(processor.lastSelection.screenId, QStringLiteral("display-B"));
        QVERIFY(source.active);
        QVERIFY(processor.running);
        QVERIFY(session.stop());
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
    }

    void selectingCancellationSequence() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [](QStringView) { return screenSentinel(); });
        QSignalSpy states(&session, &cimbarpunk::CaptureSession::stateChanged);

        QVERIFY(session.beginSelection());
        QVERIFY(session.cancel());
        QCOMPARE(session.state(), cimbarpunk::SessionState::Cancelled);
        QVERIFY(!session.beginSelection());
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);

        QCOMPARE(recordedStates(states), QList<int>({selecting, cancelled, idle}));
        QCOMPARE(occurrences(recordedStates(states), idle), 1);
    }

    void adjustingCancellationSequence() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [](QStringView) { return screenSentinel(); });

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        QSignalSpy states(&session, &cimbarpunk::CaptureSession::stateChanged);
        QVERIFY(session.cancel());
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);

        QCOMPARE(recordedStates(states), QList<int>({cancelled, idle}));
        QVERIFY(!settings.restoreSelection(QStringLiteral("display-A")).has_value());
    }

    void capturingCancellationSequence() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [](QStringView) { return screenSentinel(); });

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        QVERIFY(session.confirmSelection());
        QSignalSpy states(&session, &cimbarpunk::CaptureSession::stateChanged);
        QVERIFY(session.stop());
        QCOMPARE(session.state(), cimbarpunk::SessionState::Cancelled);
        QVERIFY(!session.beginSelection());
        QVERIFY(!source.active);
        QVERIFY(!processor.running);
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);

        QCOMPARE(recordedStates(states), QList<int>({cancelled, idle}));
        QVERIFY(!settings.restoreSelection(QStringLiteral("display-A")).has_value());
    }

    void capturingErrorSequence_data() {
        QTest::addColumn<bool>("sourceReportsError");
        QTest::newRow("capture-source") << true;
        QTest::newRow("frame-processor") << false;
    }

    void capturingErrorSequence() {
        QFETCH(bool, sourceReportsError);
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [](QStringView) { return screenSentinel(); });

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        QVERIFY(session.confirmSelection());
        QSignalSpy states(&session, &cimbarpunk::CaptureSession::stateChanged);
        QSignalSpy failures(&session, &cimbarpunk::CaptureSession::failed);
        bool stoppedBeforeFailure = false;
        connect(&session, &cimbarpunk::CaptureSession::failed, &session,
            [&] { stoppedBeforeFailure = !source.active && !processor.running; });

        if (sourceReportsError) {
            source.reportFailure(QStringLiteral("source failed"));
        } else {
            processor.reportFailure(QStringLiteral("processor failed"));
        }

        QCOMPARE(session.state(), cimbarpunk::SessionState::Error);
        QVERIFY(!session.beginSelection());
        QCOMPARE(failures.size(), 1);
        QVERIFY(stoppedBeforeFailure);
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
        QCOMPARE(recordedStates(states), QList<int>({error, idle}));
        QCOMPARE(occurrences(recordedStates(states), idle), 1);
        QVERIFY(!settings.restoreSelection(QStringLiteral("display-A")).has_value());
    }

    void startupFailuresAreControlledAndCleanUp_data() {
        QTest::addColumn<int>("failureKind");
        QTest::addColumn<QList<int>>("expectedStates");
        QTest::newRow("screen-not-found") << 0 << QList<int>({error, idle});
        QTest::newRow("processor-start") << 1 << QList<int>({error, idle});
        QTest::newRow("capture-start") << 2 << QList<int>({capturing, error, idle});
    }

    void startupFailuresAreControlledAndCleanUp() {
        QFETCH(int, failureKind);
        QFETCH(QList<int>, expectedStates);
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        processor.startSucceeds = failureKind != 1;
        source.startSucceeds = failureKind != 2;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [failureKind](QStringView) { return failureKind == 0 ? nullptr : screenSentinel(); });

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        QSignalSpy states(&session, &cimbarpunk::CaptureSession::stateChanged);
        QSignalSpy failures(&session, &cimbarpunk::CaptureSession::failed);
        QVERIFY(!session.confirmSelection());
        QCOMPARE(failures.size(), 1);
        QVERIFY(!failures.first().first().toString().isEmpty());
        QVERIFY(!source.active);
        QVERIFY(!processor.running);
        QVERIFY(source.stopCalls >= 1);
        QVERIFY(processor.stopCalls >= 1);
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
        QCOMPARE(recordedStates(states), expectedStates);
    }

    void noAcceptedFrameTriggersWatchdog() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [](QStringView) { return screenSentinel(); }, 20ms);
        QSignalSpy states(&session, &cimbarpunk::CaptureSession::stateChanged);
        QSignalSpy failures(&session, &cimbarpunk::CaptureSession::failed);

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        QVERIFY(session.confirmSelection());
        QTRY_COMPARE_WITH_TIMEOUT(failures.size(), 1, 250);
        QCOMPARE(failures.first().first().toString(), QStringLiteral("连续 5 秒未收到可用画面"));
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
        QCOMPARE(recordedStates(states), QList<int>({selecting, adjusting, capturing, error, idle}));
    }

    void acceptedFramesKeepWatchdogAliveWithoutDecoderProgress() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [](QStringView) { return screenSentinel(); }, 20ms);
        QSignalSpy failures(&session, &cimbarpunk::CaptureSession::failed);
        QSignalSpy progress(&session, &cimbarpunk::CaptureSession::progressChanged);

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        QVERIFY(session.confirmSelection());
        processor.acceptFrame();

        QTimer pulse;
        pulse.setInterval(10ms);
        connect(&pulse, &QTimer::timeout, &processor, &cimbarpunk::test::FakeFrameProcessor::acceptFrame);
        QEventLoop loop;
        QTimer::singleShot(65ms, &loop, &QEventLoop::quit);
        pulse.start();
        loop.exec();
        pulse.stop();

        QVERIFY(processor.acceptedFrames >= 6);
        QCOMPARE(progress.size(), 0);
        QCOMPARE(failures.size(), 0);
        QCOMPARE(session.state(), cimbarpunk::SessionState::Capturing);
        QVERIFY(session.stop());
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
    }

    void processorProgressIsRelayedOnlyDuringCapture() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [](QStringView) { return screenSentinel(); });
        QSignalSpy progress(&session, &cimbarpunk::CaptureSession::progressChanged);

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        QVERIFY(session.confirmSelection());
        processor.reportProgress(0.4);
        QCOMPARE(progress.size(), 1);
        QCOMPARE(progress.first().first().toDouble(), 0.4);

        QVERIFY(session.stop());
        processor.reportProgress(0.8);
        QCOMPARE(progress.size(), 1);
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
    }

    void unsuccessfulCompletionUsesErrorCleanup() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        cimbarpunk::CaptureSession session(source, processor, settings,
            [](QStringView) { return screenSentinel(); });
        QSignalSpy successes(&session, &cimbarpunk::CaptureSession::completed);
        QSignalSpy failures(&session, &cimbarpunk::CaptureSession::failed);

        QVERIFY(session.beginSelection());
        QVERIFY(session.selectionCreated(selection()));
        QVERIFY(session.confirmSelection());
        processor.succeed({.error = QStringLiteral("commit failed")});

        QCOMPARE(successes.size(), 0);
        QCOMPARE(failures.size(), 1);
        QCOMPARE(failures.first().first().toString(), QStringLiteral("commit failed"));
        QVERIFY(!source.active);
        QVERIFY(!processor.running);
        QTRY_COMPARE(session.state(), cimbarpunk::SessionState::Idle);
    }

    void shutdownAndDestructionStopBothDependencies() {
        QTemporaryDir directory;
        QSettings rawSettings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore settings(rawSettings);
        cimbarpunk::test::FakeCaptureSource source;
        cimbarpunk::test::FakeFrameProcessor processor;
        {
            auto session = std::make_unique<cimbarpunk::CaptureSession>(source, processor, settings,
                [](QStringView) { return screenSentinel(); });
            QVERIFY(session->beginSelection());
            QVERIFY(session->selectionCreated(selection()));
            QVERIFY(session->confirmSelection());
            session->shutdown();
            QVERIFY(!source.active);
            QVERIFY(!processor.running);
            QVERIFY(!session->beginSelection());

            QMetaObject::invokeMethod(&processor,
                [&processor] { processor.reportFailure(QStringLiteral("late queued failure")); },
                Qt::QueuedConnection);
        }

        QCoreApplication::processEvents();
        QVERIFY(!source.active);
        QVERIFY(!processor.running);
        QVERIFY(source.stopCalls >= 1);
        QVERIFY(processor.stopCalls >= 1);
    }
};

QTEST_GUILESS_MAIN(CaptureSessionTest)
#include "tst_session.moc"
