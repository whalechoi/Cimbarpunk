// SPDX-License-Identifier: GPL-3.0-only
#include "pipeline/LatestFrameMailbox.h"

#include <QDeadlineTimer>
#include <QMutexLocker>
#include <QtTest/QTest>

#include <chrono>
#include <future>
#include <thread>

using cimbarpunk::LatestFrameMailbox;
using namespace std::chrono_literals;

namespace cimbarpunk {

class LatestFrameMailboxTestAccess final {
public:
    static bool waitUntilTakeBlocks(LatestFrameMailbox& mailbox) {
        QMutexLocker locker(&mailbox.m_mutex);
        QDeadlineTimer deadline(2000);
        while (mailbox.m_waitingTakeCount == 0) {
            if (!mailbox.m_waiterStateChanged.wait(&mailbox.m_mutex, deadline)) {
                return false;
            }
        }
        return true;
    }
};

} // namespace cimbarpunk

class LatestFrameMailboxTest final : public QObject {
    Q_OBJECT

private slots:
    void replacesPendingFramesAndCountsEachDrop() {
        LatestFrameMailbox mailbox;
        QImage red(2, 2, QImage::Format_RGB32);
        red.fill(Qt::red);
        QImage green(2, 2, QImage::Format_RGB32);
        green.fill(Qt::green);
        QImage blue(2, 2, QImage::Format_RGB32);
        blue.fill(Qt::blue);

        mailbox.replace(red);
        mailbox.replace(green);
        mailbox.replace(blue);

        const auto result = mailbox.take();
        QVERIFY(result.has_value());
        QCOMPARE(result->pixelColor(0, 0), QColor(Qt::blue));
        QCOMPARE(mailbox.droppedCount(), quint64(2));
    }

    void stopUnblocksAWaitingTakeWithinTheDeadline() {
        LatestFrameMailbox mailbox;
        std::promise<std::optional<QImage>> resultPromise;
        auto result = resultPromise.get_future();
        std::jthread waiter([&] {
            resultPromise.set_value(mailbox.take());
        });

        const bool blocked = cimbarpunk::LatestFrameMailboxTestAccess::waitUntilTakeBlocks(mailbox);
        mailbox.stop();
        const bool completed = result.wait_for(250ms) == std::future_status::ready;
        if (!completed) {
            mailbox.reset();
            QImage rescueFrame(1, 1, QImage::Format_RGB32);
            rescueFrame.fill(Qt::black);
            mailbox.replace(rescueFrame);
        }
        waiter.join();
        const auto taken = result.get();

        QVERIFY2(blocked, "take() did not reach its blocking wait");
        QVERIFY2(completed, "stop() did not unblock take() within 250 ms");
        QVERIFY(!taken.has_value());
    }

    void resetClearsPendingFrameAndDropCount() {
        LatestFrameMailbox mailbox;
        QImage red(1, 1, QImage::Format_RGB32);
        red.fill(Qt::red);
        QImage green(1, 1, QImage::Format_RGB32);
        green.fill(Qt::green);
        mailbox.replace(red);
        mailbox.replace(green);
        QCOMPARE(mailbox.droppedCount(), quint64(1));

        mailbox.reset();
        const quint64 droppedAfterReset = mailbox.droppedCount();
        std::promise<std::optional<QImage>> resultPromise;
        auto result = resultPromise.get_future();
        std::jthread waiter([&] {
            resultPromise.set_value(mailbox.take());
        });

        const bool blocked = cimbarpunk::LatestFrameMailboxTestAccess::waitUntilTakeBlocks(mailbox);
        mailbox.stop();
        const bool completed = result.wait_for(250ms) == std::future_status::ready;
        if (!completed) {
            mailbox.reset();
            QImage rescueFrame(1, 1, QImage::Format_RGB32);
            rescueFrame.fill(Qt::black);
            mailbox.replace(rescueFrame);
        }
        waiter.join();
        const auto taken = result.get();

        QCOMPARE(droppedAfterReset, quint64(0));
        QVERIFY2(blocked, "reset() left a stale pending frame available to take()");
        QVERIFY2(completed, "cleanup stop() did not unblock take() within 250 ms");
        QVERIFY(!taken.has_value());
    }

    void blockedTakeCannotCrossAnImmediateStopResetBoundary() {
        LatestFrameMailbox mailbox;
        std::promise<std::optional<QImage>> oldResultPromise;
        auto oldResult = oldResultPromise.get_future();
        std::jthread oldWaiter([&] {
            oldResultPromise.set_value(mailbox.take());
        });

        const bool blocked = cimbarpunk::LatestFrameMailboxTestAccess::waitUntilTakeBlocks(mailbox);
        mailbox.stop();
        mailbox.reset();
        QImage newFrame(1, 1, QImage::Format_RGB32);
        newFrame.fill(Qt::yellow);
        mailbox.replace(newFrame);

        const bool completed = oldResult.wait_for(250ms) == std::future_status::ready;
        if (!completed) {
            mailbox.stop();
        }
        oldWaiter.join();
        const auto oldTaken = oldResult.get();

        QVERIFY2(blocked, "take() did not reach its blocking wait");
        QVERIFY2(completed, "the old take() did not observe stop() before reset()");
        QVERIFY2(!oldTaken.has_value(), "the old take() consumed a frame from the reset session");

        std::promise<std::optional<QImage>> newResultPromise;
        auto newResult = newResultPromise.get_future();
        std::jthread newWaiter([&] {
            newResultPromise.set_value(mailbox.take());
        });
        const bool newCompleted = newResult.wait_for(250ms) == std::future_status::ready;
        if (!newCompleted) {
            mailbox.stop();
        }
        newWaiter.join();
        const auto newTaken = newResult.get();

        QVERIFY2(newCompleted, "the reset session could not take its new frame within 250 ms");
        QVERIFY(newTaken.has_value());
        QCOMPARE(newTaken->pixelColor(0, 0), QColor(Qt::yellow));
    }
};

QTEST_GUILESS_MAIN(LatestFrameMailboxTest)
#include "tst_latest_frame_mailbox.moc"
