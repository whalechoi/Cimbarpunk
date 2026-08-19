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

namespace {

QByteArray boundedRecord(const QStringView message, const qsizetype maximumBytes) {
    if (maximumBytes <= 0) {
        return {};
    }

    QString text = message.toString();
    QByteArray encoded = text.toUtf8();
    if (!encoded.endsWith('\n')) {
        encoded.append('\n');
    }
    if (encoded.size() <= maximumBytes) {
        return encoded;
    }

    if (text.endsWith('\n')) {
        text.chop(1);
    }

    QByteArray bounded;
    const qsizetype payloadLimit = maximumBytes - 1;
    for (qsizetype index = 0; index < text.size();) {
        qsizetype codeUnitCount = 1;
        if (text.at(index).isHighSurrogate() && index + 1 < text.size() && text.at(index + 1).isLowSurrogate()) {
            codeUnitCount = 2;
        }

        const QByteArray codePoint = QStringView(text).sliced(index, codeUnitCount).toString().toUtf8();
        if (bounded.size() + codePoint.size() > payloadLimit) {
            break;
        }
        bounded.append(codePoint);
        index += codeUnitCount;
    }
    bounded.append('\n');
    return bounded;
}

} // namespace

RotatingLogger::RotatingLogger(QString directory, const qsizetype maximumFileBytes)
    : RotatingLogger(std::move(directory), maximumFileBytes, std::make_unique<QFile>()) {
}

RotatingLogger::RotatingLogger(QString directory, const qsizetype maximumFileBytes, std::unique_ptr<QFile> file)
    : m_directory(std::move(directory))
    , m_maximumFileBytes(maximumFileBytes)
    , m_file(std::move(file))
    , m_mutex(std::make_unique<QMutex>()) {
    if (!m_file) {
        m_file = std::make_unique<QFile>();
    }
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
    return m_file->open(QIODevice::WriteOnly | QIODevice::Append);
}

void RotatingLogger::write(const QStringView message) {
    const QMutexLocker locker(m_mutex.get());
    if (!m_file->isOpen()) {
        return;
    }

    const QByteArray encoded = boundedRecord(message, m_maximumFileBytes);
    if (encoded.isEmpty()) {
        return;
    }
    if (m_file->size() > 0 && m_file->size() + encoded.size() > m_maximumFileBytes && !rotate()) {
        return;
    }

    const qint64 written = m_file->write(encoded);
    const bool flushed = written == encoded.size() && m_file->flush();
    if (written != encoded.size() || !flushed) {
        m_file->close();
    }
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
                m_file->open(QIODevice::WriteOnly | QIODevice::Append);
                return false;
            }
        }
    }

    QFile::remove(currentPath + QStringLiteral(".1"));
    if (!QFile::rename(currentPath, currentPath + QStringLiteral(".1"))) {
        m_file->setFileName(currentPath);
        m_file->open(QIODevice::WriteOnly | QIODevice::Append);
        return false;
    }

    m_file->setFileName(currentPath);
    return m_file->open(QIODevice::WriteOnly | QIODevice::Append);
}

} // namespace cimbarpunk
