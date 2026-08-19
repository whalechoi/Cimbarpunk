// SPDX-License-Identifier: GPL-3.0-only
#include "selection/ScreenIdentity.h"

#include <QCryptographicHash>
#include <QScreen>

namespace cimbarpunk {

QString ScreenIdentity::fromParts(const QString& manufacturer, const QString& model, const QString& serial,
    const QString& name, const QRect& geometry, qreal devicePixelRatio) {
    if (!manufacturer.isEmpty() && !model.isEmpty() && !serial.isEmpty()) {
        const auto encode = [](const QString& value) {
            return QString::number(value.size()) + u':' + value;
        };
        return QStringLiteral("primary:") + encode(manufacturer) + encode(model) + encode(serial);
    }

    const QString canonical = name + u'|' + QString::number(geometry.x()) + u'|' + QString::number(geometry.y())
        + u'|' + QString::number(geometry.width()) + u'|' + QString::number(geometry.height()) + u'|'
        + QString::number(devicePixelRatio, 'g', 17);
    const QByteArray digest = QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("fallback:") + QString::fromLatin1(digest);
}

QString ScreenIdentity::fromScreen(const QScreen& screen) {
    return fromParts(screen.manufacturer(), screen.model(), screen.serialNumber(), screen.name(), screen.geometry(),
        screen.devicePixelRatio());
}

} // namespace cimbarpunk
