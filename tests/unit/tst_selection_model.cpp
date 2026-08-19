// SPDX-License-Identifier: GPL-3.0-only
#include "selection/ScreenIdentity.h"
#include "selection/SelectionModel.h"

#include <QtTest/QTest>

using cimbarpunk::ResizeHandle;
using cimbarpunk::ScreenIdentity;
using cimbarpunk::SelectionModel;

class SelectionModelTest final : public QObject {
    Q_OBJECT

private slots:
    void normalizesReversedDragAndEnforcesMinimumSize() {
        SelectionModel model;
        model.setScreenGeometry(QRectF(0, 0, 1920, 1080));

        model.beginDrag(QPointF(500, 400));
        model.updateDrag(QPointF(100, 100));
        QCOMPARE(model.selection(), QRectF(100, 100, 400, 300));

        model.beginDrag(QPointF(10, 10));
        model.updateDrag(QPointF(20, 24));
        QCOMPARE(model.selection(), QRectF(10, 10, 32, 32));
    }

    void clampsMovementOnNegativeCoordinateScreen() {
        SelectionModel model;
        model.setScreenGeometry(QRectF(-1920, 0, 1920, 1080));
        model.setSelection(QRectF(-1900, 100, 100, 100));

        model.moveBy(QPointF(-100, 0));
        QCOMPARE(model.selection(), QRectF(-1920, 100, 100, 100));

        model.moveBy(QPointF(9999, 0));
        QCOMPARE(model.selection(), QRectF(-100, 100, 100, 100));
    }

    void resizesEachHandleWithTheOppositeEdgeFixed_data() {
        QTest::addColumn<ResizeHandle>("handle");
        QTest::addColumn<QRectF>("expected");
        QTest::newRow("top-left") << ResizeHandle::TopLeft << QRectF(120, 130, 180, 120);
        QTest::newRow("top") << ResizeHandle::Top << QRectF(100, 130, 200, 120);
        QTest::newRow("top-right") << ResizeHandle::TopRight << QRectF(100, 130, 220, 120);
        QTest::newRow("right") << ResizeHandle::Right << QRectF(100, 100, 220, 150);
        QTest::newRow("bottom-right") << ResizeHandle::BottomRight << QRectF(100, 100, 220, 180);
        QTest::newRow("bottom") << ResizeHandle::Bottom << QRectF(100, 100, 200, 180);
        QTest::newRow("bottom-left") << ResizeHandle::BottomLeft << QRectF(120, 100, 180, 180);
        QTest::newRow("left") << ResizeHandle::Left << QRectF(120, 100, 180, 150);
    }

    void resizesEachHandleWithTheOppositeEdgeFixed() {
        QFETCH(ResizeHandle, handle);
        QFETCH(QRectF, expected);
        SelectionModel model;
        model.setScreenGeometry(QRectF(0, 0, 500, 500));
        model.setSelection(QRectF(100, 100, 200, 150));

        model.resizeBy(handle, QPointF(20, 30));

        QCOMPARE(model.selection(), expected);
    }

    void preventsResizeBelowThirtyTwoLogicalPixels() {
        SelectionModel model;
        model.setScreenGeometry(QRectF(0, 0, 500, 500));
        model.setSelection(QRectF(100, 100, 200, 150));

        model.resizeBy(ResizeHandle::TopLeft, QPointF(999, 999));

        QCOMPARE(model.selection(), QRectF(268, 218, 32, 32));
    }

    void persistsAndRestoresNormalizedCoordinates() {
        SelectionModel model;
        model.setScreenGeometry(QRectF(-1920, 0, 1920, 1080));
        model.setSelection(QRectF(-1440, 270, 960, 540));

        QCOMPARE(model.normalizedRect(), QRectF(0.25, 0.25, 0.5, 0.5));

        SelectionModel restored;
        restored.setScreenGeometry(QRectF(-1920, 0, 1920, 1080));
        QVERIFY(restored.restoreNormalized(QRectF(0.25, 0.25, 0.5, 0.5)));
        QCOMPARE(restored.selection(), QRectF(-1440, 270, 960, 540));
    }

    void rejectsInvalidNormalizedCoordinates() {
        SelectionModel model;
        model.setScreenGeometry(QRectF(0, 0, 1000, 1000));
        model.setSelection(QRectF(100, 100, 100, 100));

        QVERIFY(!model.restoreNormalized(QRectF(-0.1, 0, 0.2, 0.2)));
        QVERIFY(!model.restoreNormalized(QRectF(0, 0, 0, 0.2)));
        QVERIFY(!model.restoreNormalized(QRectF(qQNaN(), 0, 0.2, 0.2)));
        QCOMPARE(model.selection(), QRectF(100, 100, 100, 100));
    }

    void mapsLogicalSelectionToActualFramePixels() {
        SelectionModel model;
        model.setScreenGeometry(QRectF(-1920, 0, 1920, 1080));
        model.setSelection(QRectF(-1440, 270, 960, 540));
        QCOMPARE(model.mapToFrame(QSize(2560, 1440)), QRect(640, 360, 1280, 720));
    }

    void mapsAxesWithIndependentPhysicalScalesAndOutwardRounding() {
        SelectionModel model;
        model.setScreenGeometry(QRectF(-1920, 0, 1920, 1080));
        model.setSelection(QRectF(-1439.5, 270.25, 960.1, 540.1));

        QCOMPARE(model.mapToFrame(QSize(2560, 1080)), QRect(640, 270, 1281, 541));
    }

    void convertsSelectionToSessionValue() {
        SelectionModel model;
        model.setScreenGeometry(QRectF(-1920, 0, 1920, 1080));
        model.setSelection(QRectF(-1440, 270, 960, 540));

        const auto selection = model.toSelection(QStringLiteral("screen-id"));
        QCOMPARE(selection.screenId, QStringLiteral("screen-id"));
        QCOMPARE(selection.screenGeometry, QRectF(-1920, 0, 1920, 1080));
        QCOMPARE(selection.logicalRect, QRectF(-1440, 270, 960, 540));
    }

    void prefersPhysicalScreenMetadataWhenComplete() {
        QCOMPARE(ScreenIdentity::fromParts(QStringLiteral("Acme"), QStringLiteral("Panel"),
                     QStringLiteral("SN-42"), QStringLiteral("DISPLAY1"), QRect(-1920, 0, 1920, 1080), 1.25),
            QStringLiteral("Acme:Panel:SN-42"));
    }

    void hashesFallbackScreenIdentityStably() {
        QCOMPARE(ScreenIdentity::fromParts(QString(), QStringLiteral("Panel"), QStringLiteral("SN-42"),
                     QStringLiteral("Screen-1"), QRect(-1920, 0, 1920, 1080), 1.25),
            QStringLiteral("fallback:fcf1628257cfce10520243fe4843bc5caf9290c29b479fcf4c5c50aedb3724fd"));
    }
};

QTEST_GUILESS_MAIN(SelectionModelTest)
#include "tst_selection_model.moc"
