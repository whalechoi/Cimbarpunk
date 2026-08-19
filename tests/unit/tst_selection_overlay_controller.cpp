// SPDX-License-Identifier: GPL-3.0-only
#include "selection/SelectionOverlayController.h"
#include "selection/ScreenIdentity.h"
#include "settings/SettingsStore.h"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <algorithm>

namespace {

QQuickItem* itemByName(QObject* root, const char* name) {
    return root->findChild<QQuickItem*>(QString::fromLatin1(name));
}

QRectF itemGeometry(const QQuickItem* item) {
    return QRectF(item->x(), item->y(), item->width(), item->height());
}

QColor sampleLocalPixel(const QImage& image, const QSizeF& logicalSize, const QPointF& point) {
    const int x = std::clamp(qFloor(point.x() * image.width() / logicalSize.width()),
        0, image.width() - 1);
    const int y = std::clamp(qFloor(point.y() * image.height() / logicalSize.height()),
        0, image.height() - 1);
    return image.pixelColor(x, y);
}

} // namespace

class SelectionOverlayControllerTest final : public QObject {
    Q_OBJECT

private slots:
    void init() {
        QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
    }

    void viewMatchesTheSelectedScreenWithoutCreatingATaskbarWindow() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;

        controller.showForScreen(screen, std::nullopt);

        QVERIFY(controller.m_view != nullptr);
        QCOMPARE(controller.m_view->status(), QQuickView::Ready);
        QVERIFY(controller.m_view->errors().isEmpty());
        QVERIFY(controller.m_view->rootObject() != nullptr);
        QCOMPARE(controller.m_view->rootObject()->property("selectionModel").value<QObject*>(),
            static_cast<QObject*>(&controller.m_model));
        QCOMPARE(controller.m_view->screen(), screen);
        QCOMPARE(controller.m_view->geometry(), screen->geometry());
        QCOMPARE(controller.m_view->type(), Qt::Tool);
        QVERIFY(controller.m_view->flags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(controller.m_view->flags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(controller.m_view->isVisible());
    }

    void restoresANormalizedSelectionOnlyWhenThePersistedScreenMatches() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QSettings settings(directory.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
        cimbarpunk::SettingsStore store(settings);
        const QString screenId = cimbarpunk::ScreenIdentity::fromScreen(*screen);
        const QRectF normalized(0.125, 0.25, 0.5, 0.5);
        cimbarpunk::SelectionOverlayController controller;

        store.saveSelection(QStringLiteral("another-screen"), normalized);
        controller.showForScreen(screen, store.restoreSelection(screenId));
        QVERIFY(!controller.m_model.hasSelection());

        store.saveSelection(screenId, normalized);
        controller.showForScreen(screen, store.restoreSelection(screenId));
        QVERIFY(controller.m_model.hasSelection());
        QCOMPARE(controller.m_model.normalizedRect(), normalized);
    }

    void forwardsOneAcceptanceForTheCurrentSelection() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, QRectF(0.25, 0.25, 0.5, 0.5));
        QSignalSpy accepted(&controller, &cimbarpunk::SelectionOverlayController::accepted);
        QSignalSpy cancelled(&controller, &cimbarpunk::SelectionOverlayController::cancelled);
        QObject* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);

        QVERIFY(QMetaObject::invokeMethod(root, "acceptRequested", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(root, "acceptRequested", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(root, "cancelRequested", Qt::DirectConnection));

        QCOMPARE(accepted.count(), 1);
        QCOMPARE(cancelled.count(), 0);
        const auto selection = qvariant_cast<cimbarpunk::ScreenSelection>(accepted.at(0).at(0));
        QCOMPARE(selection.screenId, cimbarpunk::ScreenIdentity::fromScreen(*screen));
        QCOMPARE(selection.screenGeometry, QRectF(screen->geometry()));
        QCOMPARE(selection.logicalRect,
            QRectF(screen->geometry().x() + screen->geometry().width() * 0.25,
                screen->geometry().y() + screen->geometry().height() * 0.25,
                screen->geometry().width() * 0.5, screen->geometry().height() * 0.5));
    }

    void cancellationIsEmittedOnceAndHidesTheOverlay() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, QRectF(0.25, 0.25, 0.5, 0.5));
        QSignalSpy cancelled(&controller, &cimbarpunk::SelectionOverlayController::cancelled);
        QObject* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);

        QVERIFY(QMetaObject::invokeMethod(root, "cancelRequested", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(root, "cancelRequested", Qt::DirectConnection));

        QCOMPARE(cancelled.count(), 1);
        QVERIFY(!controller.m_view->isVisible());
    }

    void hideReleasesMouseAndKeyboardGrabs() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, QRectF(0.25, 0.25, 0.5, 0.5));
        QQuickItem* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents();
        QTest::mousePress(controller.m_view.get(), Qt::LeftButton, Qt::NoModifier,
            QPoint(10, 10));
        root->forceActiveFocus();
        QVERIFY(controller.m_view->mouseGrabberItem() != nullptr);
        QCOMPARE(controller.m_view->activeFocusItem(), root);

        controller.hide();

        QVERIFY(controller.m_view->mouseGrabberItem() == nullptr);
        QVERIFY(controller.m_view->activeFocusItem() != root);
        QVERIFY(!controller.m_view->isVisible());
    }

    void reshowCancelsAHeldBackgroundDragBeforeRestoringTheNewSelection() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, std::nullopt);
        QQuickItem* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents();

        QTest::mousePress(controller.m_view.get(), Qt::LeftButton, Qt::NoModifier,
            QPoint(60, 60));
        QTest::mouseMove(controller.m_view.get(), QPoint(180, 180), 5);
        QTRY_VERIFY(controller.m_view->mouseGrabberItem() != nullptr);
        QVERIFY(controller.m_model.hasSelection());

        controller.showForScreen(screen, QRectF(0.25, 0.25, 0.5, 0.5));
        const QRectF restoredSelection = controller.m_model.selection();
        QVERIFY(QMetaObject::invokeMethod(root, "dragUpdated", Qt::DirectConnection,
            Q_ARG(QPointF, QPointF(420, 360))));
        QTest::mouseMove(controller.m_view.get(), QPoint(430, 370), 5);
        QTest::mouseRelease(controller.m_view.get(), Qt::LeftButton, Qt::NoModifier,
            QPoint(430, 370));
        QCoreApplication::processEvents();

        QCOMPARE(controller.m_model.selection(), restoredSelection);
        QVERIFY(controller.m_view->mouseGrabberItem() == nullptr);
    }

    void convertsQmlLocalDragCoordinatesOnANegativeXScreen() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForResolvedScreen(screen, QStringLiteral("negative-screen"),
            QRect(-1920, 0, 1920, 1080), std::nullopt);
        QSignalSpy accepted(&controller, &cimbarpunk::SelectionOverlayController::accepted);
        QObject* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);

        QVERIFY(QMetaObject::invokeMethod(root, "dragStarted", Qt::DirectConnection,
            Q_ARG(QPointF, QPointF(100, 100))));
        QVERIFY(QMetaObject::invokeMethod(root, "dragUpdated", Qt::DirectConnection,
            Q_ARG(QPointF, QPointF(500, 400))));
        QVERIFY(QMetaObject::invokeMethod(root, "dragEnded", Qt::DirectConnection));
        QCOMPARE(controller.m_model.selection(), QRectF(-1820, 100, 400, 300));
        QCOMPARE(root->property("selectionRect").toRectF(), QRectF(100, 100, 400, 300));
        QVERIFY(QMetaObject::invokeMethod(root, "acceptRequested", Qt::DirectConnection));

        QCOMPARE(accepted.count(), 1);
        const auto selection = qvariant_cast<cimbarpunk::ScreenSelection>(accepted.at(0).at(0));
        QCOMPARE(selection.screenId, QStringLiteral("negative-screen"));
        QCOMPARE(selection.screenGeometry, QRectF(-1920, 0, 1920, 1080));
        QCOMPARE(selection.logicalRect, QRectF(-1820, 100, 400, 300));
    }

    void forwardsMoveDeltasAndResizeHandleEnumsToTheModel() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForResolvedScreen(screen, QStringLiteral("negative-screen"),
            QRect(-1920, 0, 1920, 1080), QRectF(0.25, 0.25, 0.5, 0.5));
        QObject* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);

        QVERIFY(QMetaObject::invokeMethod(root, "moveRequested", Qt::DirectConnection,
            Q_ARG(QPointF, QPointF(20, -10))));
        QVERIFY(QMetaObject::invokeMethod(root, "resizeRequested", Qt::DirectConnection,
            Q_ARG(int, static_cast<int>(cimbarpunk::ResizeHandle::Right)),
            Q_ARG(QPointF, QPointF(40, 0))));

        QCOMPARE(controller.m_model.selection(), QRectF(-1420, 260, 1000, 540));
        QCOMPARE(root->property("selectionRect").toRectF(), QRectF(500, 260, 1000, 540));
    }

    void enterShortcutAcceptsExactlyOnce() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, QRectF(0.25, 0.25, 0.5, 0.5));
        QSignalSpy accepted(&controller, &cimbarpunk::SelectionOverlayController::accepted);
        QTRY_VERIFY(controller.m_view->isActive());
        QTRY_VERIFY(controller.m_view->rootObject()->hasActiveFocus());

        QTest::keyClick(controller.m_view.get(), Qt::Key_Return);
        QTest::keyClick(controller.m_view.get(), Qt::Key_Enter);

        QCOMPARE(accepted.count(), 1);
    }

    void escapeShortcutCancelsAndHides() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, QRectF(0.25, 0.25, 0.5, 0.5));
        QSignalSpy cancelled(&controller, &cimbarpunk::SelectionOverlayController::cancelled);
        QTRY_VERIFY(controller.m_view->isActive());
        QTRY_VERIFY(controller.m_view->rootObject()->hasActiveFocus());

        QTest::keyClick(controller.m_view.get(), Qt::Key_Escape);

        QCOMPARE(cancelled.count(), 1);
        QVERIFY(!controller.m_view->isVisible());
    }

    void qmlBuildsDimmingBordersHandlesMoveAreaAndToolbarAroundTheCrop() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, QRectF(0.25, 0.25, 0.5, 0.5));
        QQuickItem* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents();
        const QRectF crop = root->property("selectionRect").toRectF();

        QQuickItem* dimTop = itemByName(root, "dimTop");
        QQuickItem* dimBottom = itemByName(root, "dimBottom");
        QQuickItem* dimLeft = itemByName(root, "dimLeft");
        QQuickItem* dimRight = itemByName(root, "dimRight");
        QVERIFY(dimTop != nullptr);
        QVERIFY(dimBottom != nullptr);
        QVERIFY(dimLeft != nullptr);
        QVERIFY(dimRight != nullptr);
        QCOMPARE(itemGeometry(dimTop), QRectF(0, 0, root->width(), crop.y()));
        QCOMPARE(itemGeometry(dimBottom),
            QRectF(0, crop.bottom(), root->width(), root->height() - crop.bottom()));
        QCOMPARE(itemGeometry(dimLeft), QRectF(0, crop.y(), crop.x(), crop.height()));
        QCOMPARE(itemGeometry(dimRight),
            QRectF(crop.right(), crop.y(), root->width() - crop.right(), crop.height()));

        QQuickItem* borderTop = itemByName(root, "borderTop");
        QQuickItem* borderBottom = itemByName(root, "borderBottom");
        QQuickItem* borderLeft = itemByName(root, "borderLeft");
        QQuickItem* borderRight = itemByName(root, "borderRight");
        QVERIFY(borderTop != nullptr);
        QVERIFY(borderBottom != nullptr);
        QVERIFY(borderLeft != nullptr);
        QVERIFY(borderRight != nullptr);
        const qreal borderGap = root->property("borderGap").toReal();
        QVERIFY(borderGap * controller.m_view->devicePixelRatio() >= 1.0);
        QCOMPARE(itemGeometry(borderTop),
            QRectF(crop.x(), crop.y() - borderGap - 2, crop.width(), 2));
        QCOMPARE(itemGeometry(borderBottom),
            QRectF(crop.x(), crop.bottom() + borderGap, crop.width(), 2));
        QCOMPARE(itemGeometry(borderLeft),
            QRectF(crop.x() - borderGap - 2, crop.y(), 2, crop.height()));
        QCOMPARE(itemGeometry(borderRight),
            QRectF(crop.right() + borderGap, crop.y(), 2, crop.height()));

        for (int handle = 0; handle < 8; ++handle) {
            QQuickItem* item = itemByName(root,
                qPrintable(QStringLiteral("resizeHandle%1").arg(handle)));
            QVERIFY(item != nullptr);
            QCOMPARE(item->width(), 12.0);
            QCOMPARE(item->height(), 12.0);
            QVERIFY(item->isVisible());
        }

        QQuickItem* moveArea = itemByName(root, "moveArea");
        QQuickItem* toolbar = itemByName(root, "toolbar");
        QQuickItem* startButton = itemByName(root, "startButton");
        QQuickItem* cancelButton = itemByName(root, "cancelButton");
        QVERIFY(moveArea != nullptr);
        QCOMPARE(itemGeometry(moveArea), crop);
        QVERIFY(toolbar != nullptr);
        QVERIFY(!itemGeometry(toolbar).intersects(crop));
        QVERIFY(startButton != nullptr);
        QCOMPARE(startButton->property("text").toString(), QStringLiteral("开始"));
        QVERIFY(cancelButton != nullptr);
        QCOMPARE(cancelButton->property("text").toString(), QStringLiteral("取消"));
    }

    void captureModeRemovesAdjustmentUiAndMakesTheWindowMouseTransparent() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, QRectF(0.25, 0.25, 0.5, 0.5));
        QQuickItem* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);
        const QRectF before = controller.m_model.selection();

        controller.enterCaptureMode();
        QCoreApplication::processEvents();

        QVERIFY(root->property("captureMode").toBool());
        QVERIFY(controller.m_view->flags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(controller.m_view->isVisible());
        QVERIFY(!itemByName(root, "dimTop")->isVisible());
        QVERIFY(!itemByName(root, "dimBottom")->isVisible());
        QVERIFY(!itemByName(root, "dimLeft")->isVisible());
        QVERIFY(!itemByName(root, "dimRight")->isVisible());
        QVERIFY(!itemByName(root, "moveArea")->isVisible());
        QVERIFY(!itemByName(root, "toolbar")->isVisible());
        for (int handle = 0; handle < 8; ++handle) {
            QVERIFY(!itemByName(root,
                qPrintable(QStringLiteral("resizeHandle%1").arg(handle)))->isVisible());
        }
        QCOMPARE(itemByName(root, "selectionBackground")->property("enabled").toBool(), false);
        QVERIFY(itemByName(root, "borderTop")->isVisible());

        QVERIFY(QMetaObject::invokeMethod(root, "moveRequested", Qt::DirectConnection,
            Q_ARG(QPointF, QPointF(100, 100))));
        QCOMPARE(controller.m_model.selection(), before);
    }

    void adjustmentToolbarUsesOnlyAFullyFittingSide_data() {
        QTest::addColumn<QRectF>("normalized");
        QTest::addColumn<QString>("side");
        QTest::addColumn<bool>("visible");
        QTest::newRow("below-top-edge") << QRectF(0.25, 0, 0.5, 0.5)
                                          << QStringLiteral("below") << true;
        QTest::newRow("above-bottom-edge") << QRectF(0.25, 0.5, 0.5, 0.5)
                                             << QStringLiteral("above") << true;
        QTest::newRow("right-left-edge") << QRectF(0, 0, 0.75, 1)
                                           << QStringLiteral("right") << true;
        QTest::newRow("left-right-edge") << QRectF(0.25, 0, 0.75, 1)
                                           << QStringLiteral("left") << true;
        QTest::newRow("none-nearly-full") << QRectF(0.025, 0.025, 0.95, 0.95)
                                            << QString() << false;
        QTest::newRow("none-exact-full") << QRectF(0, 0, 1, 1) << QString() << false;
    }

    void adjustmentToolbarUsesOnlyAFullyFittingSide() {
        QFETCH(QRectF, normalized);
        QFETCH(QString, side);
        QFETCH(bool, visible);
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, normalized);
        QCoreApplication::processEvents();
        QQuickItem* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);
        QQuickItem* toolbar = itemByName(root, "toolbar");
        QVERIFY(toolbar != nullptr);

        QCOMPARE(root->property("toolbarVisible").toBool(), visible);
        QCOMPARE(root->property("toolbarSide").toString(), side);
        QCOMPARE(toolbar->isVisible(), visible);
        if (visible) {
            const QRectF toolbarRect = root->property("toolbarRect").toRectF();
            const QRectF crop = root->property("selectionRect").toRectF();
            const QRectF bounds(0, 0, root->width(), root->height());
            QCOMPARE(itemGeometry(toolbar), toolbarRect);
            QVERIFY(bounds.contains(toolbarRect));
            QVERIFY(!toolbarRect.intersects(crop));
        }
    }

    void captureModeHidesExactlyFullScreenButNotNearlyFullScreen() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;

        controller.showForScreen(screen, QRectF(0, 0, 1, 0.999));
        controller.enterCaptureMode();
        QVERIFY(controller.m_view->isVisible());

        controller.showForScreen(screen, QRectF(0, 0, 1, 1));
        controller.enterCaptureMode();
        QVERIFY(!controller.m_view->isVisible());
    }

    void captureStatusUsesOnlyAFullyFittingSide_data() {
        QTest::addColumn<QRectF>("normalized");
        QTest::addColumn<QString>("side");
        QTest::addColumn<bool>("visible");
        QTest::newRow("above") << QRectF(0.25, 0.25, 0.5, 0.5)
                               << QStringLiteral("above") << true;
        QTest::newRow("below") << QRectF(0.25, 0, 0.5, 0.5)
                               << QStringLiteral("below") << true;
        QTest::newRow("left") << QRectF(0.25, 0, 0.75, 1)
                              << QStringLiteral("left") << true;
        QTest::newRow("right") << QRectF(0, 0, 0.75, 1)
                               << QStringLiteral("right") << true;
        QTest::newRow("none") << QRectF(0.025, 0.025, 0.95, 0.95)
                              << QString() << false;
    }

    void captureStatusUsesOnlyAFullyFittingSide() {
        QFETCH(QRectF, normalized);
        QFETCH(QString, side);
        QFETCH(bool, visible);
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, normalized);
        controller.enterCaptureMode();
        QCoreApplication::processEvents();
        QQuickItem* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);

        QCOMPARE(root->property("statusVisible").toBool(), visible);
        QCOMPARE(root->property("statusSide").toString(), side);
        if (visible) {
            const QRectF status = root->property("statusRect").toRectF();
            const QRectF crop = root->property("selectionRect").toRectF();
            const QRectF bounds(0, 0, root->width(), root->height());
            QVERIFY2(bounds.contains(status),
                qPrintable(QStringLiteral("bounds=%1,%2 %3x%4 status=%5,%6 %7x%8")
                    .arg(bounds.x()).arg(bounds.y()).arg(bounds.width()).arg(bounds.height())
                    .arg(status.x()).arg(status.y()).arg(status.width()).arg(status.height())));
            QVERIFY(!status.intersects(crop));
        }
    }

    void progressTextDoesNotInventAPercentageWhenProgressIsUnknown() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, QRectF(0.25, 0.25, 0.5, 0.5));
        controller.enterCaptureMode();
        QQuickItem* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);
        QCOMPARE(root->property("statusText").toString(), QStringLiteral("正在识别"));

        controller.setProgress(0.42);
        QCOMPARE(root->property("statusText").toString(), QStringLiteral("42%"));

        controller.setProgress(std::nullopt);
        QCOMPARE(root->property("statusText").toString(), QStringLiteral("正在识别"));
    }

    void captureStatusHidesWhenItCannotFitWithinTheScreenOnTheOtherAxis() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForResolvedScreen(screen, QStringLiteral("narrow-screen"),
            QRect(0, 0, 120, 300), QRectF(0, 0.5, 1, 0.2));
        controller.enterCaptureMode();
        QCoreApplication::processEvents();
        QQuickItem* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);

        QVERIFY(!root->property("statusVisible").toBool());
        QCOMPARE(root->property("statusSide").toString(), QString());
    }

    void qmlMoveAreaAndRightHandleSendWindowLocalPointerDeltas() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, QRectF(0.25, 0.25, 0.5, 0.5));
        QQuickItem* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents();
        const QRectF initial = root->property("selectionRect").toRectF();
        const QPoint moveStart = initial.center().toPoint();
        const QPoint moveEnd = moveStart + QPoint(20, 10);

        QTest::mousePress(controller.m_view.get(), Qt::LeftButton, Qt::NoModifier, moveStart);
        QTest::mouseMove(controller.m_view.get(), moveEnd, 5);
        QTest::mouseRelease(controller.m_view.get(), Qt::LeftButton, Qt::NoModifier, moveEnd);

        QTRY_COMPARE(root->property("selectionRect").toRectF(),
            initial.translated(20, 10));
        const QRectF moved = root->property("selectionRect").toRectF();
        const QPoint resizeStart(qRound(moved.right()), qRound(moved.center().y()));
        const QPoint resizeEnd = resizeStart + QPoint(30, 0);
        QTest::mousePress(controller.m_view.get(), Qt::LeftButton, Qt::NoModifier, resizeStart);
        QTest::mouseMove(controller.m_view.get(), resizeEnd, 5);
        QTest::mouseRelease(controller.m_view.get(), Qt::LeftButton, Qt::NoModifier, resizeEnd);

        QTRY_COMPARE(root->property("selectionRect").toRectF(),
            QRectF(moved.x(), moved.y(), moved.width() + 30, moved.height()));
    }

    void renderedOverlayKeepsTheCropTransparentAndRemovesDimmingForCapture() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, QRectF(0.25, 0.25, 0.5, 0.5));
        QQuickItem* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);
        QCoreApplication::processEvents();
        const QRectF crop = root->property("selectionRect").toRectF();
        const QSizeF logicalSize(root->width(), root->height());

        const QImage adjustment = controller.m_view->grabWindow();
        QVERIFY(!adjustment.isNull());
        QCOMPARE(sampleLocalPixel(adjustment, logicalSize, crop.center()).alpha(), 0);
        QVERIFY(sampleLocalPixel(adjustment, logicalSize, QPointF(10, 10)).alpha() > 0);

        controller.enterCaptureMode();
        QCoreApplication::processEvents();
        const QImage capture = controller.m_view->grabWindow();
        QVERIFY(!capture.isNull());
        QCOMPARE(sampleLocalPixel(capture, logicalSize, crop.center()).alpha(), 0);
        QCOMPARE(sampleLocalPixel(capture, logicalSize, QPointF(10, 10)).alpha(), 0);
    }

    void fractionalPhysicalCropKeepsOneRenderedPixelClearOfTheBorder() {
        QScreen* screen = QGuiApplication::primaryScreen();
        QVERIFY(screen != nullptr);
        const QSize screenSize = screen->geometry().size();
        const QRectF localCrop(100.75, 100.75,
            std::min(299.5, screenSize.width() - 201.0),
            std::min(199.5, screenSize.height() - 201.0));
        QVERIFY(localCrop.width() >= 24.0);
        QVERIFY(localCrop.height() >= 24.0);
        const QRectF normalized(localCrop.x() / screenSize.width(),
            localCrop.y() / screenSize.height(), localCrop.width() / screenSize.width(),
            localCrop.height() / screenSize.height());
        cimbarpunk::SelectionOverlayController controller;
        controller.showForScreen(screen, normalized);
        QQuickItem* root = controller.m_view->rootObject();
        QVERIFY(root != nullptr);
        controller.enterCaptureMode();
        QCoreApplication::processEvents();

        const QImage capture = controller.m_view->grabWindow();
        QVERIFY(!capture.isNull());
        const QSizeF logicalSize(root->width(), root->height());
        const QRectF crop = root->property("selectionRect").toRectF();
        const qreal scaleX = capture.width() / logicalSize.width();
        const qreal scaleY = capture.height() / logicalSize.height();
        const qreal borderGap = root->property("borderGap").toReal();
        const QRect physicalCrop(qFloor(crop.x() * scaleX), qFloor(crop.y() * scaleY),
            qCeil(crop.right() * scaleX) - qFloor(crop.x() * scaleX),
            qCeil(crop.bottom() * scaleY) - qFloor(crop.y() * scaleY));
        QVERIFY(QRect(QPoint(0, 0), capture.size()).contains(physicalCrop));
        for (int y = physicalCrop.top(); y <= physicalCrop.bottom(); ++y) {
            for (int x = physicalCrop.left(); x <= physicalCrop.right(); ++x) {
                if (capture.pixelColor(x, y).alpha() != 0) {
                    QFAIL(qPrintable(QStringLiteral(
                        "crop pixel (%1,%2) has alpha %3; crop=%4,%5 %6x%7")
                                         .arg(x)
                                         .arg(y)
                                         .arg(capture.pixelColor(x, y).alpha())
                                         .arg(physicalCrop.x())
                                         .arg(physicalCrop.y())
                                         .arg(physicalCrop.width())
                                         .arg(physicalCrop.height())));
                }
            }
        }
        QVERIFY2(borderGap * std::min(scaleX, scaleY) >= 1.0,
            qPrintable(QStringLiteral("gap=%1 scale=%2x%3")
                           .arg(borderGap)
                           .arg(scaleX)
                           .arg(scaleY)));

        const QRectF topBorder = itemGeometry(itemByName(root, "borderTop"));
        QVERIFY(sampleLocalPixel(capture, logicalSize, topBorder.center()).alpha() > 0);
    }
};

QTEST_MAIN(SelectionOverlayControllerTest)

#include "tst_selection_overlay_controller.moc"
