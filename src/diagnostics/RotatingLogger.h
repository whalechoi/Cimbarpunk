// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QStringView>

class QImage;
class QFile;
class QMutex;

#include <memory>

namespace cimbarpunk {

class RotatingLogger final {
public:
    static constexpr qsizetype defaultMaximumFileBytes = 1024 * 1024;
    static constexpr int backupFileCount = 3;

    explicit RotatingLogger(QString directory = {}, qsizetype maximumFileBytes = defaultMaximumFileBytes);
    RotatingLogger(QString directory, qsizetype maximumFileBytes, std::unique_ptr<QFile> file);
    ~RotatingLogger();

    RotatingLogger(const RotatingLogger&) = delete;
    RotatingLogger& operator=(const RotatingLogger&) = delete;

    [[nodiscard]] bool install();
    void write(QStringView message);
    void writeImageDiagnostics(const QImage& image);
    void uninstall();

private:
    [[nodiscard]] QString currentFilePath() const;
    [[nodiscard]] bool rotate();

    QString m_directory;
    qsizetype m_maximumFileBytes;
    std::unique_ptr<QFile> m_file;
    std::unique_ptr<QMutex> m_mutex;
};

} // namespace cimbarpunk
