// SPDX-License-Identifier: GPL-3.0-only
#include "capture/ICaptureSource.h"
#include "core/SessionTypes.h"
#include "decoder/IDecoder.h"
#include "output/IOutputStore.h"
#include "pipeline/IFrameProcessor.h"

#include <QtTest/QTest>

class ContractsTest final : public QObject {
    Q_OBJECT

private slots:
    void payloadPreservesSenderMetadata() {
        const cimbarpunk::DecodedPayload value{
            .suggestedName = QStringLiteral("report.txt"),
            .fallbackName = QStringLiteral("17.4096"),
            .compressedBytes = QByteArray("zstd")
        };
        QCOMPARE(value.suggestedName, QStringLiteral("report.txt"));
        QCOMPARE(value.fallbackName, QStringLiteral("17.4096"));
    }
};

QTEST_GUILESS_MAIN(ContractsTest)
#include "tst_contracts.moc"
