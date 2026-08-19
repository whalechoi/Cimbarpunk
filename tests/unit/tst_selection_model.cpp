// SPDX-License-Identifier: GPL-3.0-only
#include "selection/ScreenIdentity.h"
#include "selection/SelectionModel.h"

#include <QtTest/QSignalSpy>
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

    void constrainsActiveResizeEdgesWithoutMovingOppositeEdges_data() {
        QTest::addColumn<ResizeHandle>("handle");
        QTest::addColumn<QPointF>("delta");
        QTest::addColumn<QRectF>("expected");
        QTest::newRow("top-left") << ResizeHandle::TopLeft << QPointF(-1000, -1000) << QRectF(0, 0, 300, 250);
        QTest::newRow("top") << ResizeHandle::Top << QPointF(0, -1000) << QRectF(100, 0, 200, 250);
        QTest::newRow("top-right") << ResizeHandle::TopRight << QPointF(1000, -1000) << QRectF(100, 0, 400, 250);
        QTest::newRow("right") << ResizeHandle::Right << QPointF(1000, 0) << QRectF(100, 100, 400, 150);
        QTest::newRow("bottom-right") << ResizeHandle::BottomRight << QPointF(1000, 1000) << QRectF(100, 100, 400, 400);
        QTest::newRow("bottom") << ResizeHandle::Bottom << QPointF(0, 1000) << QRectF(100, 100, 200, 400);
        QTest::newRow("bottom-left") << ResizeHandle::BottomLeft << QPointF(-1000, 1000) << QRectF(0, 100, 300, 400);
        QTest::newRow("left") << ResizeHandle::Left << QPointF(-1000, 0) << QRectF(0, 100, 300, 150);
    }

    void constrainsActiveResizeEdgesWithoutMovingOppositeEdges() {
        QFETCH(ResizeHandle, handle);
        QFETCH(QPointF, delta);
        QFETCH(QRectF, expected);
        SelectionModel model;
        model.setScreenGeometry(QRectF(0, 0, 500, 500));
        model.setSelection(QRectF(100, 100, 200, 150));

        model.resizeBy(handle, delta);

        QCOMPARE(model.selection(), expected);
    }

    void enforcesMinimumSizeForEveryResizeHandle_data() {
        QTest::addColumn<ResizeHandle>("handle");
        QTest::addColumn<QPointF>("delta");
        QTest::addColumn<QRectF>("expected");
        QTest::newRow("top-left") << ResizeHandle::TopLeft << QPointF(999, 999) << QRectF(268, 218, 32, 32);
        QTest::newRow("top") << ResizeHandle::Top << QPointF(0, 999) << QRectF(100, 218, 200, 32);
        QTest::newRow("top-right") << ResizeHandle::TopRight << QPointF(-999, 999) << QRectF(100, 218, 32, 32);
        QTest::newRow("right") << ResizeHandle::Right << QPointF(-999, 0) << QRectF(100, 100, 32, 150);
        QTest::newRow("bottom-right") << ResizeHandle::BottomRight << QPointF(-999, -999) << QRectF(100, 100, 32, 32);
        QTest::newRow("bottom") << ResizeHandle::Bottom << QPointF(0, -999) << QRectF(100, 100, 200, 32);
        QTest::newRow("bottom-left") << ResizeHandle::BottomLeft << QPointF(999, -999) << QRectF(268, 100, 32, 32);
        QTest::newRow("left") << ResizeHandle::Left << QPointF(999, 0) << QRectF(268, 100, 32, 150);
    }

    void enforcesMinimumSizeForEveryResizeHandle() {
        QFETCH(ResizeHandle, handle);
        QFETCH(QPointF, delta);
        QFETCH(QRectF, expected);
        SelectionModel model;
        model.setScreenGeometry(QRectF(0, 0, 500, 500));
        model.setSelection(QRectF(100, 100, 200, 150));

        model.resizeBy(handle, delta);

        QCOMPARE(model.selection(), expected);
    }

    void clampsAnOversizedSelectionToTheWholeScreen() {
        SelectionModel model;
        model.setScreenGeometry(QRectF(-1920, 0, 1920, 1080));

        model.setSelection(QRectF(-4000, -1000, 8000, 4000));

        QCOMPARE(model.selection(), QRectF(-1920, 0, 1920, 1080));
        QVERIFY(model.hasSelection());
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

    void rejectsInvalidNormalizedCoordinates_data() {
        QTest::addColumn<QRectF>("normalized");
        QTest::newRow("left outside") << QRectF(-0.1, 0, 0.2, 0.2);
        QTest::newRow("top outside") << QRectF(0, -0.1, 0.2, 0.2);
        QTest::newRow("right outside") << QRectF(0.9, 0, 0.2, 0.2);
        QTest::newRow("bottom outside") << QRectF(0, 0.9, 0.2, 0.2);
        QTest::newRow("zero width") << QRectF(0, 0, 0, 0.2);
        QTest::newRow("negative width") << QRectF(0.5, 0, -0.2, 0.2);
        QTest::newRow("zero height") << QRectF(0, 0, 0.2, 0);
        QTest::newRow("negative height") << QRectF(0, 0.5, 0.2, -0.2);
        QTest::newRow("nan coordinate") << QRectF(qQNaN(), 0, 0.2, 0.2);
        QTest::newRow("infinite coordinate") << QRectF(qInf(), 0, 0.2, 0.2);
        QTest::newRow("nan dimension") << QRectF(0, 0, qQNaN(), 0.2);
        QTest::newRow("infinite dimension") << QRectF(0, 0, 0.2, qInf());
    }

    void rejectsInvalidNormalizedCoordinates() {
        QFETCH(QRectF, normalized);
        SelectionModel model;
        model.setScreenGeometry(QRectF(0, 0, 1000, 1000));
        model.setSelection(QRectF(100, 100, 100, 100));

        QVERIFY(!model.restoreNormalized(normalized));
        QCOMPARE(model.selection(), QRectF(100, 100, 100, 100));
    }

    void publishesAReclampedSelectionWithNewScreenGeometry() {
        SelectionModel model;
        model.setScreenGeometry(QRectF(0, 0, 500, 500));
        model.setSelection(QRectF(400, 400, 100, 100));
        QSignalSpy screenGeometrySpy(&model, &SelectionModel::screenGeometryChanged);
        QRectF selectionObservedAtGeometryChange;
        connect(&model, &SelectionModel::screenGeometryChanged, this, [&] {
            selectionObservedAtGeometryChange = model.selection();
        });

        model.setScreenGeometry(QRectF(0, 0, 300, 300));

        QCOMPARE(screenGeometrySpy.count(), 1);
        QCOMPARE(selectionObservedAtGeometryChange, QRectF(200, 200, 100, 100));
        QCOMPARE(model.selection(), QRectF(200, 200, 100, 100));
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
            QStringLiteral("primary:4:Acme5:Panel5:SN-42"));
    }

    void distinguishesDelimiterContainingPhysicalScreenMetadata() {
        const QString first = ScreenIdentity::fromParts(QStringLiteral("A:B"), QStringLiteral("C"),
            QStringLiteral("D"), QStringLiteral("DISPLAY1"), QRect(-1920, 0, 1920, 1080), 1.25);
        const QString second = ScreenIdentity::fromParts(QStringLiteral("A"), QStringLiteral("B:C"),
            QStringLiteral("D"), QStringLiteral("DISPLAY1"), QRect(-1920, 0, 1920, 1080), 1.25);

        QVERIFY(first != second);
    }

    void hashesFallbackScreenIdentityStably() {
        QCOMPARE(ScreenIdentity::fromParts(QString(), QStringLiteral("Panel"), QStringLiteral("SN-42"),
                     QStringLiteral("Screen-1"), QRect(-1920, 0, 1920, 1080), 1.25),
            QStringLiteral("fallback:fcf1628257cfce10520243fe4843bc5caf9290c29b479fcf4c5c50aedb3724fd"));
    }
};

QTEST_GUILESS_MAIN(SelectionModelTest)
#include "tst_selection_model.moc"
