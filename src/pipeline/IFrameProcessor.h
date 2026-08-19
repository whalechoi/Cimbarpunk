// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "core/SessionTypes.h"

#include <QImage>
#include <QObject>
#include <QString>

namespace cimbarpunk {

class IFrameProcessor : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~IFrameProcessor() override = default;
    virtual bool start(const ScreenSelection& selection, const QString& outputDirectory, QString* error) = 0;
    virtual void submitFrame(const QImage& fullScreenFrame) = 0;
    virtual void stop() = 0;

signals:
    void frameAccepted();
    void progressChanged(double progress);
    void completed(const OutputResult& result);
    void failed(const QString& message);
};

} // namespace cimbarpunk
