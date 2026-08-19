// SPDX-License-Identifier: GPL-3.0-only
#include "selection/SelectionModel.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace cimbarpunk {

SelectionModel::SelectionModel(QObject* parent)
    : QObject(parent) {
}

QRectF SelectionModel::screenGeometry() const {
    return m_screenGeometry;
}

void SelectionModel::setScreenGeometry(const QRectF& geometry) {
    const QRectF next = isUsableRect(geometry) ? geometry : QRectF();
    if (m_screenGeometry == next) {
        return;
    }

    m_screenGeometry = next;
    emit screenGeometryChanged();
    if (m_hasSelection) {
        applySelection(clampRect(m_selection));
    }
}

QRectF SelectionModel::selection() const {
    return m_selection;
}

void SelectionModel::setSelection(const QRectF& selection) {
    applySelection(clampRect(selection));
}

bool SelectionModel::hasSelection() const {
    return m_hasSelection;
}

void SelectionModel::beginDrag(const QPointF& point) {
    if (!isUsableRect(m_screenGeometry) || !isFinite(point)) {
        return;
    }
    m_dragAnchor = point;
    m_dragging = true;
}

void SelectionModel::updateDrag(const QPointF& point) {
    if (!m_dragging || !isFinite(point)) {
        return;
    }

    qreal left = std::min(m_dragAnchor.x(), point.x());
    qreal right = std::max(m_dragAnchor.x(), point.x());
    qreal top = std::min(m_dragAnchor.y(), point.y());
    qreal bottom = std::max(m_dragAnchor.y(), point.y());
    if (right - left < minimumSize) {
        if (point.x() >= m_dragAnchor.x()) {
            right = m_dragAnchor.x() + minimumSize;
        } else {
            left = m_dragAnchor.x() - minimumSize;
        }
    }
    if (bottom - top < minimumSize) {
        if (point.y() >= m_dragAnchor.y()) {
            bottom = m_dragAnchor.y() + minimumSize;
        } else {
            top = m_dragAnchor.y() - minimumSize;
        }
    }
    applySelection(clampRect(QRectF(left, top, right - left, bottom - top)));
}

void SelectionModel::endDrag() {
    m_dragging = false;
}

void SelectionModel::moveBy(const QPointF& delta) {
    if (!m_hasSelection || !isFinite(delta)) {
        return;
    }
    applySelection(clampRect(m_selection.translated(delta)));
}

void SelectionModel::resizeBy(ResizeHandle handle, const QPointF& delta) {
    if (!m_hasSelection || !isFinite(delta)) {
        return;
    }

    qreal left = m_selection.left();
    qreal top = m_selection.top();
    qreal right = m_selection.right();
    qreal bottom = m_selection.bottom();
    switch (handle) {
    case ResizeHandle::TopLeft:
        left = std::min(left + delta.x(), right - minimumSize);
        top = std::min(top + delta.y(), bottom - minimumSize);
        break;
    case ResizeHandle::Top:
        top = std::min(top + delta.y(), bottom - minimumSize);
        break;
    case ResizeHandle::TopRight:
        right = std::max(right + delta.x(), left + minimumSize);
        top = std::min(top + delta.y(), bottom - minimumSize);
        break;
    case ResizeHandle::Right:
        right = std::max(right + delta.x(), left + minimumSize);
        break;
    case ResizeHandle::BottomRight:
        right = std::max(right + delta.x(), left + minimumSize);
        bottom = std::max(bottom + delta.y(), top + minimumSize);
        break;
    case ResizeHandle::Bottom:
        bottom = std::max(bottom + delta.y(), top + minimumSize);
        break;
    case ResizeHandle::BottomLeft:
        left = std::min(left + delta.x(), right - minimumSize);
        bottom = std::max(bottom + delta.y(), top + minimumSize);
        break;
    case ResizeHandle::Left:
        left = std::min(left + delta.x(), right - minimumSize);
        break;
    }
    applySelection(clampRect(QRectF(left, top, right - left, bottom - top)));
}

QRectF SelectionModel::normalizedRect() const {
    if (!m_hasSelection || !isUsableRect(m_screenGeometry)) {
        return {};
    }
    return QRectF((m_selection.x() - m_screenGeometry.x()) / m_screenGeometry.width(),
        (m_selection.y() - m_screenGeometry.y()) / m_screenGeometry.height(),
        m_selection.width() / m_screenGeometry.width(), m_selection.height() / m_screenGeometry.height());
}

bool SelectionModel::restoreNormalized(const QRectF& normalized) {
    if (!isFinite(normalized) || normalized.x() < 0.0 || normalized.y() < 0.0 || normalized.width() <= 0.0
        || normalized.height() <= 0.0 || normalized.right() > 1.0 || normalized.bottom() > 1.0
        || !isUsableRect(m_screenGeometry)) {
        return false;
    }
    const QRectF restored(m_screenGeometry.x() + normalized.x() * m_screenGeometry.width(),
        m_screenGeometry.y() + normalized.y() * m_screenGeometry.height(),
        normalized.width() * m_screenGeometry.width(), normalized.height() * m_screenGeometry.height());
    const QRectF clamped = clampRect(restored);
    if (!isUsableRect(clamped)) {
        return false;
    }
    applySelection(clamped);
    return true;
}

ScreenSelection SelectionModel::toSelection(const QString& screenId) const {
    return ScreenSelection{screenId, m_screenGeometry, m_selection};
}

QRect SelectionModel::mapToFrame(const QSize& frameSize) const {
    if (!m_hasSelection || !isUsableRect(m_screenGeometry) || !frameSize.isValid()) {
        return {};
    }
    const double sx = static_cast<double>(frameSize.width()) / m_screenGeometry.width();
    const double sy = static_cast<double>(frameSize.height()) / m_screenGeometry.height();
    const int left = qFloor((m_selection.x() - m_screenGeometry.x()) * sx);
    const int top = qFloor((m_selection.y() - m_screenGeometry.y()) * sy);
    const int right = qCeil((m_selection.x() + m_selection.width() - m_screenGeometry.x()) * sx);
    const int bottom = qCeil((m_selection.y() + m_selection.height() - m_screenGeometry.y()) * sy);
    return QRect(QPoint(left, top), QPoint(right - 1, bottom - 1))
        .intersected(QRect(QPoint(0, 0), frameSize));
}

bool SelectionModel::isFinite(const QPointF& point) {
    return std::isfinite(point.x()) && std::isfinite(point.y());
}

bool SelectionModel::isFinite(const QRectF& rect) {
    return isFinite(rect.topLeft()) && std::isfinite(rect.width()) && std::isfinite(rect.height())
        && std::isfinite(rect.right()) && std::isfinite(rect.bottom());
}

bool SelectionModel::isUsableRect(const QRectF& rect) {
    return isFinite(rect) && rect.width() > 0.0 && rect.height() > 0.0;
}

QRectF SelectionModel::clampRect(const QRectF& rect) const {
    if (!isUsableRect(rect) || !isUsableRect(m_screenGeometry) || m_screenGeometry.width() < minimumSize
        || m_screenGeometry.height() < minimumSize) {
        return {};
    }
    const qreal width = std::clamp(rect.width(), minimumSize, m_screenGeometry.width());
    const qreal height = std::clamp(rect.height(), minimumSize, m_screenGeometry.height());
    const qreal left = std::clamp(rect.x(), m_screenGeometry.left(), m_screenGeometry.right() - width);
    const qreal top = std::clamp(rect.y(), m_screenGeometry.top(), m_screenGeometry.bottom() - height);
    return QRectF(left, top, width, height);
}

void SelectionModel::applySelection(const QRectF& rect) {
    const bool hasSelection = isUsableRect(rect);
    const QRectF next = hasSelection ? rect : QRectF();
    if (m_selection != next) {
        m_selection = next;
        emit selectionChanged();
    }
    if (m_hasSelection != hasSelection) {
        m_hasSelection = hasSelection;
        emit hasSelectionChanged();
    }
}

} // namespace cimbarpunk
