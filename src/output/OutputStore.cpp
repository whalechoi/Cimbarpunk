// SPDX-License-Identifier: GPL-3.0-only
#include "output/OutputStore.h"

#include "settings/SettingsStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUuid>

#include <exception>

namespace cimbarpunk {

namespace {

bool isWindowsReservedName(const QString& filename) {
    const QString basename = QFileInfo(filename).completeBaseName();
    if (basename.compare(QStringLiteral("CON"), Qt::CaseInsensitive) == 0
        || basename.compare(QStringLiteral("PRN"), Qt::CaseInsensitive) == 0
        || basename.compare(QStringLiteral("AUX"), Qt::CaseInsensitive) == 0
        || basename.compare(QStringLiteral("NUL"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    if (basename.size() != 4) {
        return false;
    }
    const QString prefix = basename.left(3).toUpper();
    return (prefix == QStringLiteral("COM") || prefix == QStringLiteral("LPT"))
        && basename.at(3) >= QLatin1Char('1')
        && basename.at(3) <= QLatin1Char('9');
}

QString absoluteDirectory(const QString& directory) {
    return QDir(directory).absolutePath();
}

QString temporaryPathFor(const QString& directory) {
    return QDir(directory).absoluteFilePath(
        QStringLiteral(".cimbarpunk-%1.part").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

bool isRegisteredTemporaryPath(const QFileInfo& fileInfo, const QString& outputDirectory) {
    static const QRegularExpression temporaryFilename(
        QStringLiteral(R"(^\.cimbarpunk-[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\.part$)"),
        QRegularExpression::CaseInsensitiveOption);
    return fileInfo.isAbsolute()
        && fileInfo.absolutePath() == outputDirectory
        && temporaryFilename.match(fileInfo.fileName()).hasMatch();
}

} // namespace

QString sanitizeFilename(const QStringView senderFilename) {
    QString filename = QFileInfo(senderFilename.toString()).fileName();
    for (QChar& character : filename) {
        if (character.unicode() < 32
            || character == QLatin1Char('<')
            || character == QLatin1Char('>')
            || character == QLatin1Char(':')
            || character == QLatin1Char('"')
            || character == QLatin1Char('/')
            || character == QLatin1Char('\\')
            || character == QLatin1Char('|')
            || character == QLatin1Char('?')
            || character == QLatin1Char('*')) {
            character = QLatin1Char('_');
        }
    }
    while (!filename.isEmpty() && (filename.endsWith(QLatin1Char('.')) || filename.endsWith(QLatin1Char(' ')))) {
        filename.chop(1);
    }

    if (filename.isEmpty() || filename == QStringLiteral(".") || filename == QStringLiteral("..") || isWindowsReservedName(filename)) {
        return {};
    }
    return filename;
}

QString uniqueDestination(const QStringView directory, const QStringView filename) {
    const QString safeFilename = sanitizeFilename(filename);
    if (safeFilename.isEmpty()) {
        return {};
    }

    const QDir targetDirectory(directory.toString());
    const QFileInfo fileInfo(safeFilename);
    const QString extension = fileInfo.suffix();
    const QString basename = extension.isEmpty() ? safeFilename : fileInfo.completeBaseName();
    const QString suffix = extension.isEmpty() ? QString() : QStringLiteral(".") + extension;

    QString candidate = targetDirectory.absoluteFilePath(safeFilename);
    for (int collisionNumber = 1; QFile::exists(candidate); ++collisionNumber) {
        candidate = targetDirectory.absoluteFilePath(
            QStringLiteral("%1 (%2)%3").arg(basename).arg(collisionNumber).arg(suffix));
    }
    return candidate;
}

OutputStore::OutputStore(SettingsStore& settingsStore, PayloadWriter writer)
    : m_settingsStore(settingsStore)
    , m_writer(std::move(writer)) {
}

bool OutputStore::prepareDirectory(const QString& directory, QString* error) {
    const QString absolutePath = absoluteDirectory(directory);
    if (directory.isEmpty() || !QDir().mkpath(absolutePath)) {
        if (error != nullptr) {
            *error = QStringLiteral("Unable to create output directory: %1").arg(absolutePath);
        }
        return false;
    }

    const QString probePath = QDir(absolutePath).absoluteFilePath(
        QStringLiteral(".cimbarpunk-probe-%1.part").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile probe(probePath);
    if (!probe.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("Output directory is not writable: %1").arg(absolutePath);
        }
        return false;
    }
    probe.close();
    if (!QFile::remove(probePath)) {
        if (error != nullptr) {
            *error = QStringLiteral("Unable to remove output directory probe: %1").arg(probePath);
        }
        return false;
    }
    return true;
}

OutputResult OutputStore::commit(const DecodedPayload& payload, const QString& directory) {
    QString error;
    if (!prepareDirectory(directory, &error)) {
        return {.error = error};
    }
    if (!m_writer) {
        return {.error = QStringLiteral("No payload writer configured")};
    }

    const QString outputDirectory = absoluteDirectory(directory);
    const QString temporaryPath = temporaryPathFor(outputDirectory);
    m_settingsStore.registerTemporaryFile(temporaryPath);
    const auto unregisterAndRemoveTemporary = [&] {
        QFile::remove(temporaryPath);
        m_settingsStore.unregisterTemporaryFile(temporaryPath);
    };

    bool writerSucceeded = false;
    try {
        writerSucceeded = m_writer(temporaryPath, QByteArrayView(payload.compressedBytes), &error);
    } catch (const std::exception& exception) {
        unregisterAndRemoveTemporary();
        return {.error = QString::fromUtf8(exception.what())};
    } catch (...) {
        unregisterAndRemoveTemporary();
        return {.error = QStringLiteral("Payload writer failed with an unknown exception")};
    }
    if (!writerSucceeded) {
        unregisterAndRemoveTemporary();
        return {.error = error.isEmpty() ? QStringLiteral("Unable to write decoded payload") : error};
    }
    if (!QFileInfo::exists(temporaryPath)) {
        unregisterAndRemoveTemporary();
        return {.error = QStringLiteral("Payload writer did not create a temporary file")};
    }

    const QString filename = sanitizeFilename(payload.suggestedName).isEmpty()
        ? fallbackFilename(payload.fallbackName)
        : sanitizeFilename(payload.suggestedName);
    const QString destination = uniqueDestination(outputDirectory, filename);
    if (destination.isEmpty() || !QFile::rename(temporaryPath, destination)) {
        unregisterAndRemoveTemporary();
        return {.error = QStringLiteral("Unable to atomically commit decoded payload")};
    }

    m_settingsStore.unregisterTemporaryFile(temporaryPath);
    return {.ok = true, .finalPath = destination};
}

void OutputStore::cleanupRegisteredTemporaryFiles() {
    const QString outputDirectory = absoluteDirectory(m_settingsStore.outputDirectory());
    const QStringList registeredPaths = m_settingsStore.registeredTemporaryFiles();
    for (const QString& path : registeredPaths) {
        const QFileInfo fileInfo(path);
        if (isRegisteredTemporaryPath(fileInfo, outputDirectory)) {
            QFile::remove(path);
        }
        m_settingsStore.unregisterTemporaryFile(path);
    }
}

QString OutputStore::fallbackFilename(const QStringView fallbackName) const {
    QString fallback = sanitizeFilename(fallbackName);
    if (fallback.isEmpty()) {
        fallback = QStringLiteral("payload");
    }
    return QStringLiteral("cimbar-%1-%2.bin")
        .arg(fallback, QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")));
}

} // namespace cimbarpunk
