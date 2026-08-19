// SPDX-License-Identifier: GPL-3.0-only
#include "selection/SelectionOverlayController.h"

#include "selection/ScreenIdentity.h"

#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace cimbarpunk {

SelectionOverlayController::SelectionOverlayController(QObject* parent)
    : QObject(parent)
    , m_model()
    , m_view(std::make_unique<QQuickView>()) {
    Q_INIT_RESOURCE(selection_overlay_qml);

    m_view->setColor(Qt::transparent);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
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
        return;
    }

    showForResolvedScreen(screen, ScreenIdentity::fromScreen(*screen), screen->geometry(),
        normalizedRect);
}

void SelectionOverlayController::showForResolvedScreen(QScreen* screen, const QString& screenId,
    const QRect& geometry, const std::optional<QRectF> normalizedRect) {
    if (screen == nullptr || screenId.isEmpty() || !geometry.isValid()) {
        return;
    }

    m_screenId = screenId;
    m_acceptEmitted = false;
    m_cancelEmitted = false;
    m_captureMode = false;
    m_view->setFlag(Qt::WindowTransparentForInput, false);
    if (QObject* root = m_view->rootObject(); root != nullptr) {
        root->setProperty("captureMode", false);
        root->setProperty("statusText", QStringLiteral("正在识别"));
    }
    m_view->setScreen(screen);
    m_view->setGeometry(geometry);
    m_model.setScreenGeometry(geometry);
    m_model.setSelection({});
    if (normalizedRect.has_value()) {
        (void)m_model.restoreNormalized(*normalizedRect);
    }
    publishSelection();
    m_view->show();
}

void SelectionOverlayController::enterCaptureMode() {
    if (!m_view->isVisible() || !m_model.hasSelection()) {
        return;
    }

    m_captureMode = true;
    releaseInputGrabs();
    if (QObject* root = m_view->rootObject(); root != nullptr) {
        root->setProperty("captureMode", true);
        root->setProperty("statusText", QStringLiteral("正在识别"));
    }
    m_view->setFlag(Qt::WindowTransparentForInput, true);

    if (m_model.normalizedRect() == QRectF(0, 0, 1, 1)) {
        m_view->hide();
    } else {
        m_view->show();
    }
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
}

void SelectionOverlayController::hide() {
    releaseInputGrabs();
    m_view->hide();
}

void SelectionOverlayController::acceptSelection() {
    if (m_acceptEmitted || !m_view->isVisible() || !m_model.hasSelection()) {
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
