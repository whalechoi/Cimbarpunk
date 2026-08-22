// SPDX-License-Identifier: GPL-3.0-only
#include "selection/SelectionOverlayController.h"

#include "selection/ScreenIdentity.h"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QVariant>

#include <algorithm>
#include <cmath>

static void initializeSelectionOverlayResource() {
    Q_INIT_RESOURCE(selection_overlay_qml);
}

namespace cimbarpunk {

SelectionOverlayController::SelectionOverlayController(QObject* parent)
    : QObject(parent)
    , m_model()
    , m_view(std::make_unique<QQuickView>()) {
    initializeSelectionOverlayResource();

    m_view->setColor(Qt::transparent);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    Qt::WindowFlags overlayFlags =
        Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint;
    if (QGuiApplication::platformName() == QStringLiteral("xcb")) {
        // X11 window managers may constrain tool windows to the desktop work area,
        // which would offset the model's screen-relative capture coordinates.
        overlayFlags |= Qt::X11BypassWindowManagerHint;
    }
    m_view->setFlags(overlayFlags);
    m_view->setInitialProperties(
        {{QStringLiteral("selectionModel"), QVariant::fromValue(&m_model)}});
    m_view->setSource(QUrl(QStringLiteral("qrc:/cimbarpunk/qml/SelectionOverlay.qml")));
    if (QObject* root = m_view->rootObject(); root != nullptr) {
        connect(root, SIGNAL(acceptRequested()), this, SLOT(acceptSelection()));
        connect(root, SIGNAL(cancelRequested()), this, SLOT(cancelSelection()));
        connect(root, SIGNAL(dragStarted(QPointF)), this, SLOT(beginDrag(QPointF)));
        connect(root, SIGNAL(dragUpdated(QPointF)), this, SLOT(updateDrag(QPointF)));
        connect(root, SIGNAL(dragEnded()), this, SLOT(endDrag()));
        connect(root, SIGNAL(moveRequested(QPointF)), this, SLOT(moveSelection(QPointF)));
        connect(root, SIGNAL(resizeRequested(int,QPointF)), this,
            SLOT(resizeSelection(int,QPointF)));
    }
    connect(&m_model, &SelectionModel::selectionChanged, this,
        &SelectionOverlayController::publishSelection);
    connect(&m_model, &SelectionModel::hasSelectionChanged, this,
        &SelectionOverlayController::publishSelection);
}

SelectionOverlayController::~SelectionOverlayController() = default;

void SelectionOverlayController::showForScreen(
    QScreen* screen, const std::optional<QRectF> normalizedRect) {
    if (screen == nullptr) {
        ++m_overlayTransitionGeneration;
        m_captureOverlayDesiredVisible = false;
        m_focusHandbackTarget.clear();
        return;
    }

    showForResolvedScreen(screen, ScreenIdentity::fromScreen(*screen), screen->geometry(),
        normalizedRect);
}

void SelectionOverlayController::showForResolvedScreen(QScreen* screen, const QString& screenId,
    const QRect& geometry, const std::optional<QRectF> normalizedRect) {
    ++m_overlayTransitionGeneration;
    m_captureOverlayDesiredVisible = false;
    m_focusHandbackTarget.clear();
    if (screen == nullptr || screenId.isEmpty() || !geometry.isValid()) {
        return;
    }

    if (QWindow* const focusWindow = QGuiApplication::focusWindow();
        focusWindow != nullptr && focusWindow != m_view.get() && focusWindow->isVisible()) {
        m_focusHandbackTarget = focusWindow;
    }
    m_model.endDrag();
    releaseInputGrabs();
    const bool resettingCaptureWindow =
        m_view->flags().testFlag(Qt::WindowTransparentForInput)
        || m_view->flags().testFlag(Qt::WindowDoesNotAcceptFocus);
    if (resettingCaptureWindow) {
        m_view->hide();
    }
    m_screenId = screenId;
    m_acceptEmitted = false;
    m_cancelEmitted = false;
    m_captureMode = false;
    m_view->setFlag(Qt::WindowTransparentForInput, false);
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, false);
    if (QObject* root = m_view->rootObject(); root != nullptr) {
        root->setProperty("captureMode", false);
        root->setProperty("statusText", QStringLiteral("正在识别"));
    }
    m_view->setScreen(screen);
    m_view->setGeometry(geometry);
    if (QObject* root = m_view->rootObject(); root != nullptr) {
        const qreal devicePixelRatio = m_view->devicePixelRatio();
        root->setProperty("borderGap",
            devicePixelRatio > 0.0 ? 1.0 / devicePixelRatio : 1.0);
    }
    m_model.setScreenGeometry(geometry);
    m_model.setSelection({});
    if (normalizedRect.has_value()) {
        (void)m_model.restoreNormalized(*normalizedRect);
    }
    publishSelection();
    m_view->show();
    m_view->requestActivate();
    if (QQuickItem* root = m_view->rootObject(); root != nullptr) {
        root->forceActiveFocus(Qt::ActiveWindowFocusReason);
    }
}

void SelectionOverlayController::enterCaptureMode() {
    const quint64 transitionGeneration = ++m_overlayTransitionGeneration;
    m_captureOverlayDesiredVisible = false;
    if (!m_view->isVisible() || !m_model.hasSelection()) {
        return;
    }

    m_captureMode = true;
    m_model.endDrag();
    releaseInputGrabs();
    if (QObject* root = m_view->rootObject(); root != nullptr) {
        root->setProperty("captureMode", true);
        root->setProperty("statusText", QStringLiteral("正在识别"));
    }
    const QRect geometry = m_view->geometry();
    QScreen* const screen = m_view->screen();
    m_view->hide();
    m_view->setFlag(Qt::WindowTransparentForInput, true);
    m_view->setFlag(Qt::WindowDoesNotAcceptFocus, true);
    m_view->setScreen(screen);
    m_view->setGeometry(geometry);

    if (m_model.normalizedRect() == QRectF(0, 0, 1, 1)) {
        return;
    }
    m_captureOverlayDesiredVisible = true;
    QQuickView* const view = m_view.get();
    const bool captureOverlayDesiredVisible = m_captureOverlayDesiredVisible;
    const QPointer<QWindow> focusHandbackTarget = m_focusHandbackTarget;
    QMetaObject::invokeMethod(view,
        [this, view, transitionGeneration, captureOverlayDesiredVisible,
            focusHandbackTarget] {
            if (transitionGeneration != m_overlayTransitionGeneration
                || !captureOverlayDesiredVisible || !m_captureOverlayDesiredVisible
                || !m_captureMode || !view->flags().testFlag(Qt::WindowTransparentForInput)
                || !view->flags().testFlag(Qt::WindowDoesNotAcceptFocus)) {
                return;
            }
            view->show();
            if (transitionGeneration != m_overlayTransitionGeneration
                || !m_captureOverlayDesiredVisible || !m_captureMode) {
                return;
            }
            if (focusHandbackTarget != nullptr && focusHandbackTarget->isVisible()) {
                focusHandbackTarget->requestActivate();
            }
        },
        Qt::QueuedConnection);
}

void SelectionOverlayController::setProgress(const std::optional<double> progress) {
    QObject* root = m_view->rootObject();
    if (root == nullptr) {
        return;
    }

    QString text = QStringLiteral("正在识别");
    if (progress.has_value() && std::isfinite(*progress)) {
        const int percentage = qRound(std::clamp(*progress, 0.0, 1.0) * 100.0);
        text = QString::number(percentage) + u'%';
    }
    root->setProperty("statusText", text);
}

void SelectionOverlayController::releaseInputGrabs() {
    if (QQuickItem* mouseGrabber = m_view->mouseGrabberItem(); mouseGrabber != nullptr) {
        mouseGrabber->ungrabMouse();
    }
    if (QQuickItem* focusItem = m_view->activeFocusItem(); focusItem != nullptr) {
        focusItem->setFocus(false);
    }
    if (QQuickItem* root = m_view->rootObject(); root != nullptr) {
        root->setFocus(false);
    }
    if (QQuickItem* contentItem = m_view->contentItem(); contentItem != nullptr) {
        contentItem->setFocus(false);
    }
}

void SelectionOverlayController::hide() {
    ++m_overlayTransitionGeneration;
    m_captureOverlayDesiredVisible = false;
    releaseInputGrabs();
    m_view->hide();
}

void SelectionOverlayController::acceptSelection() {
    if (m_acceptEmitted || m_captureMode || !m_view->isVisible() || !m_model.hasSelection()) {
        return;
    }

    m_acceptEmitted = true;
    emit accepted(m_model.toSelection(m_screenId));
}

void SelectionOverlayController::cancelSelection() {
    if (m_cancelEmitted || m_acceptEmitted || m_captureMode || !m_view->isVisible()) {
        return;
    }

    m_cancelEmitted = true;
    hide();
    emit cancelled();
}

void SelectionOverlayController::beginDrag(const QPointF& localPosition) {
    if (!m_view->isVisible() || m_acceptEmitted || m_cancelEmitted || m_captureMode) {
        return;
    }
    m_model.beginDrag(localPosition + m_model.screenGeometry().topLeft());
}

void SelectionOverlayController::updateDrag(const QPointF& localPosition) {
    if (!m_view->isVisible() || m_acceptEmitted || m_cancelEmitted || m_captureMode) {
        return;
    }
    m_model.updateDrag(localPosition + m_model.screenGeometry().topLeft());
}

void SelectionOverlayController::endDrag() {
    m_model.endDrag();
}

void SelectionOverlayController::moveSelection(const QPointF& delta) {
    if (!m_view->isVisible() || m_acceptEmitted || m_cancelEmitted || m_captureMode) {
        return;
    }
    m_model.moveBy(delta);
}

void SelectionOverlayController::resizeSelection(const int handle, const QPointF& delta) {
    constexpr int firstHandle = static_cast<int>(ResizeHandle::TopLeft);
    constexpr int lastHandle = static_cast<int>(ResizeHandle::Left);
    if (!m_view->isVisible() || m_acceptEmitted || m_cancelEmitted || m_captureMode
        || handle < firstHandle || handle > lastHandle) {
        return;
    }
    m_model.resizeBy(static_cast<ResizeHandle>(handle), delta);
}

void SelectionOverlayController::publishSelection() {
    QObject* root = m_view->rootObject();
    if (root == nullptr) {
        return;
    }

    QRectF localRect;
    if (m_model.hasSelection()) {
        localRect = m_model.selection().translated(-m_model.screenGeometry().topLeft());
    }
    root->setProperty("selectionRect", localRect);
}

} // namespace cimbarpunk

#include "moc_SelectionOverlayController.cpp"
