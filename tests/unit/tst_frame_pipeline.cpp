// SPDX-License-Identifier: GPL-3.0-only
#include "pipeline/FramePipeline.h"

#include <QtTest/QTest>

#include <algorithm>
#include <optional>
#include <vector>

using cimbarpunk::FramePipeline;
using cimbarpunk::ScreenSelection;

class FramePipelineTest final : public QObject {
    Q_OBJECT

private slots:
    void mapsCropsAndOwnsRgb888Storage() {
        FramePipeline pipeline;
        pipeline.configure(ScreenSelection{QStringLiteral("screen-id"), QRectF(-1920, 0, 1920, 1080),
            QRectF(-1440, 270, 960, 540)});

        std::optional<QImage> prepared;
        {
            std::vector<QRgb> sourceStorage(2560 * 1440, qRgba(0, 0, 0, 255));
            sourceStorage[360 * 2560 + 640] = qRgba(17, 34, 51, 68);
            sourceStorage[1079 * 2560 + 1919] = qRgba(85, 102, 119, 136);
            QImage source(reinterpret_cast<uchar*>(sourceStorage.data()), 2560, 1440, QImage::Format_ARGB32);

            prepared = pipeline.prepare(source);

            std::fill(sourceStorage.begin(), sourceStorage.end(), qRgba(255, 255, 255, 255));
        }

        QVERIFY(prepared.has_value());
        QCOMPARE(prepared->size(), QSize(1280, 720));
        QCOMPARE(prepared->format(), QImage::Format_RGB888);
        QVERIFY(prepared->isDetached());
        QCOMPARE(prepared->pixelColor(0, 0), QColor(17, 34, 51));
        QCOMPARE(prepared->pixelColor(1279, 719), QColor(85, 102, 119));
    }

    void rejectsNullFrames() {
        FramePipeline pipeline;
        pipeline.configure(ScreenSelection{QStringLiteral("screen-id"), QRectF(0, 0, 1920, 1080),
            QRectF(0, 0, 1920, 1080)});

        QVERIFY(!pipeline.prepare(QImage()).has_value());
    }

    void rejectsZeroSizedCrops() {
        FramePipeline pipeline;
        pipeline.configure(ScreenSelection{QStringLiteral("screen-id"), QRectF(0, 0, 1920, 1080),
            QRectF(100, 100, 0, 500)});
        QImage source(2560, 1440, QImage::Format_ARGB32);
        source.fill(Qt::black);

        QVERIFY(!pipeline.prepare(source).has_value());
    }
};

QTEST_GUILESS_MAIN(FramePipelineTest)
#include "tst_frame_pipeline.moc"
