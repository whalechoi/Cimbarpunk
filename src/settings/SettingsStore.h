// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <memory>
#include <optional>

class QSettings;

namespace cimbarpunk {

class SettingsStore final {
public:
    SettingsStore();

    // The caller retains ownership and must keep settings alive for this store's lifetime.
    explicit SettingsStore(QSettings& settings);
    ~SettingsStore();

    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;
    SettingsStore(SettingsStore&&) = delete;
    SettingsStore& operator=(SettingsStore&&) = delete;

    [[nodiscard]] QString outputDirectory() const;
    void setOutputDirectory(const QString& directory);

    void saveSelection(QStringView screenId, const QRectF& normalizedRect);
    [[nodiscard]] std::optional<QRectF> restoreSelection(QStringView screenId) const;

    void registerTemporaryFile(const QString& path);
    void unregisterTemporaryFile(const QString& path);
    [[nodiscard]] QStringList registeredTemporaryFiles() const;

private:
    [[nodiscard]] static bool isPersistedRect(const QRectF& rect);

    std::unique_ptr<QSettings> m_ownedSettings;
    QSettings* m_settings = nullptr;
};

} // namespace cimbarpunk
