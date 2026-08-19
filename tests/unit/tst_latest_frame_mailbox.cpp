// SPDX-License-Identifier: GPL-3.0-only
#include "pipeline/LatestFrameMailbox.h"

#include <QtTest/QTest>

#include <chrono>
#include <future>
#include <thread>

using cimbarpunk::LatestFrameMailbox;
using namespace std::chrono_literals;

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
        std::promise<void> aboutToTake;
        auto entered = aboutToTake.get_future();
        std::promise<std::optional<QImage>> resultPromise;
        auto result = resultPromise.get_future();
        std::jthread waiter([&] {
            aboutToTake.set_value();
            resultPromise.set_value(mailbox.take());
        });

        QCOMPARE(entered.wait_for(250ms), std::future_status::ready);
        mailbox.stop();
        const bool completed = result.wait_for(250ms) == std::future_status::ready;
        if (!completed) {
            mailbox.reset();
            QImage rescueFrame(1, 1, QImage::Format_RGB32);
            rescueFrame.fill(Qt::black);
            mailbox.replace(rescueFrame);
        }
        waiter.join();

        QVERIFY2(completed, "stop() did not unblock take() within 250 ms");
        QVERIFY(!result.get().has_value());
    }

    void resetAcceptsAFrameAfterStop() {
        LatestFrameMailbox mailbox;
        mailbox.stop();
        mailbox.reset();
        QImage frame(1, 1, QImage::Format_RGB32);
        frame.fill(Qt::yellow);

        mailbox.replace(frame);

        const auto result = mailbox.take();
        QVERIFY(result.has_value());
        QCOMPARE(result->pixelColor(0, 0), QColor(Qt::yellow));
    }
};

QTEST_GUILESS_MAIN(LatestFrameMailboxTest)
#include "tst_latest_frame_mailbox.moc"
