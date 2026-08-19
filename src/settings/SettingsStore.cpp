// SPDX-License-Identifier: GPL-3.0-only
#include "settings/SettingsStore.h"

#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

#include <cmath>

namespace cimbarpunk {

namespace {

constexpr auto outputDirectoryKey = "output/directory";
constexpr auto selectionScreenIdKey = "selection/screenId";
constexpr auto selectionNormalizedRectKey = "selection/normalizedRect";
constexpr auto pendingTemporaryFilesKey = "output/pendingTemporaryFiles";

QString defaultOutputDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QStringLiteral("/Cimbarpunk");
}

} // namespace

SettingsStore::SettingsStore()
    : m_ownedSettings(std::make_unique<QSettings>())
    , m_settings(m_ownedSettings.get()) {
}

SettingsStore::SettingsStore(QSettings& settings)
    : m_settings(&settings) {
}

SettingsStore::~SettingsStore() = default;

QString SettingsStore::outputDirectory() const {
    return m_settings->value(QLatin1StringView(outputDirectoryKey), defaultOutputDirectory()).toString();
}

void SettingsStore::setOutputDirectory(const QString& directory) {
    m_settings->setValue(QLatin1StringView(outputDirectoryKey), directory);
    m_settings->sync();
}

void SettingsStore::saveSelection(const QStringView screenId, const QRectF& normalizedRect) {
    const QVariantList serializedRect{normalizedRect.x(), normalizedRect.y(), normalizedRect.width(), normalizedRect.height()};
    m_settings->setValue(QLatin1StringView(selectionScreenIdKey), screenId.toString());
    m_settings->setValue(QLatin1StringView(selectionNormalizedRectKey), serializedRect);
    m_settings->sync();
}

std::optional<QRectF> SettingsStore::restoreSelection(const QStringView screenId) const {
    if (m_settings->value(QLatin1StringView(selectionScreenIdKey)).toString() != screenId) {
        return std::nullopt;
    }

    const QVariantList values = m_settings->value(QLatin1StringView(selectionNormalizedRectKey)).toList();
    if (values.size() != 4) {
        return std::nullopt;
    }

    qreal coordinates[4];
    for (qsizetype index = 0; index < values.size(); ++index) {
        bool converted = false;
        coordinates[index] = values.at(index).toDouble(&converted);
        if (!converted || !std::isfinite(coordinates[index])) {
            return std::nullopt;
        }
    }

    const QRectF rect(coordinates[0], coordinates[1], coordinates[2], coordinates[3]);
    if (!isPersistedRect(rect)) {
        return std::nullopt;
    }
    return rect;
}

void SettingsStore::registerTemporaryFile(const QString& path) {
    QStringList paths = registeredTemporaryFiles();
    if (!paths.contains(path)) {
        paths.append(path);
        m_settings->setValue(QLatin1StringView(pendingTemporaryFilesKey), paths);
    }
    m_settings->sync();
}

void SettingsStore::unregisterTemporaryFile(const QString& path) {
    QStringList paths = registeredTemporaryFiles();
    paths.removeAll(path);
    m_settings->setValue(QLatin1StringView(pendingTemporaryFilesKey), paths);
    m_settings->sync();
}

QStringList SettingsStore::registeredTemporaryFiles() const {
    return m_settings->value(QLatin1StringView(pendingTemporaryFilesKey)).toStringList();
}

bool SettingsStore::isPersistedRect(const QRectF& rect) {
    return std::isfinite(rect.x()) && std::isfinite(rect.y()) && std::isfinite(rect.width()) && std::isfinite(rect.height());
}

} // namespace cimbarpunk
