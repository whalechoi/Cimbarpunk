// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QRect>
#include <QString>

class QScreen;

namespace cimbarpunk {

class ScreenIdentity final {
public:
    [[nodiscard]] static QString fromParts(const QString& manufacturer, const QString& model, const QString& serial,
        const QString& name, const QRect& geometry, qreal devicePixelRatio);
    [[nodiscard]] static QString fromScreen(const QScreen& screen);
};

} // namespace cimbarpunk
