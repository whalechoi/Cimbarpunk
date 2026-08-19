// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "core/SessionTypes.h"

#include <QMetaType>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSize>

namespace cimbarpunk {

enum class ResizeHandle { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };

class SelectionModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QRectF screenGeometry READ screenGeometry WRITE setScreenGeometry NOTIFY screenGeometryChanged)
    Q_PROPERTY(QRectF selection READ selection WRITE setSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY hasSelectionChanged)

public:
    explicit SelectionModel(QObject* parent = nullptr);

    [[nodiscard]] QRectF screenGeometry() const;
    void setScreenGeometry(const QRectF& geometry);
    [[nodiscard]] QRectF selection() const;
    void setSelection(const QRectF& selection);
    [[nodiscard]] bool hasSelection() const;

    void beginDrag(const QPointF& point);
    void updateDrag(const QPointF& point);
    void endDrag();
    void moveBy(const QPointF& delta);
    void resizeBy(ResizeHandle handle, const QPointF& delta);

    [[nodiscard]] QRectF normalizedRect() const;
    [[nodiscard]] bool restoreNormalized(const QRectF& normalized);
    [[nodiscard]] ScreenSelection toSelection(const QString& screenId) const;
    [[nodiscard]] QRect mapToFrame(const QSize& frameSize) const;

signals:
    void screenGeometryChanged();
    void selectionChanged();
    void hasSelectionChanged();

private:
    [[nodiscard]] static bool isFinite(const QPointF& point);
    [[nodiscard]] static bool isFinite(const QRectF& rect);
    [[nodiscard]] static bool isUsableRect(const QRectF& rect);
    [[nodiscard]] QRectF clampRect(const QRectF& rect) const;
    void applySelection(const QRectF& rect);

    static constexpr qreal minimumSize = 32.0;

    QRectF m_screenGeometry;
    QRectF m_selection;
    QPointF m_dragAnchor;
    bool m_hasSelection = false;
    bool m_dragging = false;
};

} // namespace cimbarpunk

Q_DECLARE_METATYPE(cimbarpunk::ResizeHandle)
