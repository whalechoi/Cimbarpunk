// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "output/IOutputStore.h"

#include <QByteArrayView>
#include <QStringView>

#include <functional>

namespace cimbarpunk {

class SettingsStore;

using PayloadWriter = std::function<bool(const QString& temporaryPath, QByteArrayView compressedBytes, QString* error)>;

[[nodiscard]] QString sanitizeFilename(QStringView senderFilename);
[[nodiscard]] QString uniqueDestination(QStringView directory, QStringView filename);

class OutputStore final : public IOutputStore {
public:
    OutputStore(SettingsStore& settingsStore, PayloadWriter writer);

    bool prepareDirectory(const QString& directory, QString* error) override;
    [[nodiscard]] OutputResult commit(const DecodedPayload& payload, const QString& directory) override;
    void cleanupRegisteredTemporaryFiles() override;

private:
    [[nodiscard]] QString fallbackFilename(QStringView fallbackName) const;

    SettingsStore& m_settingsStore;
    PayloadWriter m_writer;
};

} // namespace cimbarpunk
