// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QRectF>
#include <QString>

#include <optional>

namespace cimbarpunk {

enum class SessionState { Idle, Selecting, Adjusting, Capturing, Completed, Error, Cancelled };

struct ScreenSelection {
    QString screenId;
    QRectF screenGeometry;
    QRectF logicalRect;
};

struct DecodedPayload {
    QString suggestedName;
    QString fallbackName;
    QByteArray compressedBytes;
};

struct DecodeUpdate {
    bool recognized = false;
    std::optional<double> progress;
    std::optional<DecodedPayload> completed;
};

struct OutputResult {
    bool ok = false;
    QString finalPath;
    QString error;
};

} // namespace cimbarpunk

Q_DECLARE_METATYPE(cimbarpunk::SessionState)
Q_DECLARE_METATYPE(cimbarpunk::ScreenSelection)
Q_DECLARE_METATYPE(cimbarpunk::DecodedPayload)
Q_DECLARE_METATYPE(cimbarpunk::OutputResult)
