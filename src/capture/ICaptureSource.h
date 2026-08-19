// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class QScreen;

namespace cimbarpunk {

class ICaptureSource : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~ICaptureSource() override = default;
    virtual bool start(QScreen* screen, QString* error) = 0;
    virtual void stop() = 0;

signals:
    void frameReady(const QImage& frame);
    void activeChanged(bool active);
    void failed(const QString& message);
};

} // namespace cimbarpunk
