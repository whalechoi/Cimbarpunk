// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "selection/SelectionModel.h"

#include <QObject>
#include <QRectF>
#include <QString>

#include <memory>
#include <optional>

class QQuickView;
class QScreen;
class SelectionOverlayControllerTest;

namespace cimbarpunk {

class SelectionOverlayController final : public QObject {
    Q_OBJECT

public:
    explicit SelectionOverlayController(QObject* parent = nullptr);
    ~SelectionOverlayController() override;

    SelectionOverlayController(const SelectionOverlayController&) = delete;
    SelectionOverlayController& operator=(const SelectionOverlayController&) = delete;

    void showForScreen(QScreen* screen, std::optional<QRectF> normalizedRect);
    void enterCaptureMode();
    void setProgress(std::optional<double> progress);
    void hide();

signals:
    void accepted(const cimbarpunk::ScreenSelection& selection);
    void cancelled();

private:
    friend class ::SelectionOverlayControllerTest;

    void showForResolvedScreen(QScreen* screen, const QString& screenId,
        const QRect& geometry, std::optional<QRectF> normalizedRect);

    Q_SLOT void acceptSelection();
    Q_SLOT void cancelSelection();
    Q_SLOT void beginDrag(const QPointF& localPosition);
    Q_SLOT void updateDrag(const QPointF& localPosition);
    Q_SLOT void endDrag();
    Q_SLOT void moveSelection(const QPointF& delta);
    Q_SLOT void resizeSelection(int handle, const QPointF& delta);
    void publishSelection();
    void releaseInputGrabs();

    SelectionModel m_model;
    std::unique_ptr<QQuickView> m_view;
    QString m_screenId;
    bool m_acceptEmitted = false;
    bool m_cancelEmitted = false;
    bool m_captureMode = false;
};

} // namespace cimbarpunk
