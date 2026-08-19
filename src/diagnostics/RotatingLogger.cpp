// SPDX-License-Identifier: GPL-3.0-only
#include "diagnostics/RotatingLogger.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QMetaEnum>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

namespace cimbarpunk {

RotatingLogger::RotatingLogger(QString directory, const qsizetype maximumFileBytes)
    : m_directory(std::move(directory))
    , m_maximumFileBytes(maximumFileBytes)
    , m_file(std::make_unique<QFile>())
    , m_mutex(std::make_unique<QMutex>()) {
    if (m_directory.isEmpty()) {
        m_directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
}

RotatingLogger::~RotatingLogger() {
    uninstall();
}

bool RotatingLogger::install() {
    const QMutexLocker locker(m_mutex.get());
    if (m_file->isOpen()) {
        return true;
    }
    if (!QDir().mkpath(m_directory)) {
        return false;
    }

    m_file->setFileName(currentFilePath());
    return m_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void RotatingLogger::write(const QStringView message) {
    const QMutexLocker locker(m_mutex.get());
    if (!m_file->isOpen()) {
        return;
    }

    QByteArray encoded = message.toString().toUtf8();
    if (!encoded.endsWith('\n')) {
        encoded.append('\n');
    }
    if (m_maximumFileBytes <= 0) {
        return;
    }
    if (encoded.size() > m_maximumFileBytes) {
        encoded.truncate(m_maximumFileBytes);
    }
    if (m_file->size() > 0 && m_file->size() + encoded.size() > m_maximumFileBytes && !rotate()) {
        return;
    }

    m_file->write(encoded);
    m_file->flush();
}

void RotatingLogger::writeImageDiagnostics(const QImage& image) {
    const QMetaEnum formats = QMetaEnum::fromType<QImage::Format>();
    const char* formatName = formats.valueToKey(image.format());
    write(QStringLiteral("image dimensions=%1x%2 format=%3")
              .arg(image.width())
              .arg(image.height())
              .arg(QString::fromLatin1(formatName == nullptr ? "Unknown" : formatName)));
}

void RotatingLogger::uninstall() {
    const QMutexLocker locker(m_mutex.get());
    if (m_file->isOpen()) {
        m_file->close();
    }
}

QString RotatingLogger::currentFilePath() const {
    return QDir(m_directory).filePath(QStringLiteral("cimbarpunk.log"));
}

bool RotatingLogger::rotate() {
    const QString currentPath = currentFilePath();
    m_file->close();

    QFile::remove(currentPath + QStringLiteral(".3"));
    for (int backup = backupFileCount - 1; backup >= 1; --backup) {
        const QString source = currentPath + QStringLiteral(".%1").arg(backup);
        const QString destination = currentPath + QStringLiteral(".%1").arg(backup + 1);
        if (QFile::exists(source)) {
            QFile::remove(destination);
            if (!QFile::rename(source, destination)) {
                m_file->setFileName(currentPath);
                m_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
                return false;
            }
        }
    }

    QFile::remove(currentPath + QStringLiteral(".1"));
    if (!QFile::rename(currentPath, currentPath + QStringLiteral(".1"))) {
        m_file->setFileName(currentPath);
        m_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        return false;
    }

    m_file->setFileName(currentPath);
    return m_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

} // namespace cimbarpunk
