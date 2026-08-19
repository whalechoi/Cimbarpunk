// SPDX-License-Identifier: GPL-3.0-only

#include "cimb_translator/Config.h"
#include "encoder/EncoderPlus.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr auto kSourceSize = qsizetype{32768};
constexpr char kSourceDomain[] = "cimbarpunk-task7-fixture-v1";

QByteArray deterministicSource()
{
    QByteArray source;
    source.reserve(kSourceSize);

    for (std::uint64_t counter = 0; source.size() < kSourceSize; ++counter) {
        QByteArray input(kSourceDomain, sizeof(kSourceDomain) - 1);
        for (int byte = 7; byte >= 0; --byte) {
            input.append(static_cast<char>((counter >> (byte * 8)) & 0xff));
        }
        source.append(QCryptographicHash::hash(input, QCryptographicHash::Sha256));
    }

    source.truncate(kSourceSize);
    return source;
}

bool writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "failed to open " << path.toStdString() << '\n';
        return false;
    }
    return file.write(contents) == contents.size() && file.flush();
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 2) {
        std::cerr << "usage: generate_cimbar_fixture <fixture-directory>\n";
        return 2;
    }

    const QDir fixtureDirectory(application.arguments().at(1));
    if (!QDir().mkpath(fixtureDirectory.absolutePath())) {
        std::cerr << "failed to create fixture directory\n";
        return 3;
    }

    const QString modeDirectoryPath = fixtureDirectory.filePath(QStringLiteral("mode68"));
    QDir modeDirectory(modeDirectoryPath);
    if (modeDirectory.exists() && !modeDirectory.removeRecursively()) {
        std::cerr << "failed to replace mode68 fixture directory\n";
        return 4;
    }
    if (!QDir().mkpath(modeDirectoryPath)) {
        std::cerr << "failed to create mode68 fixture directory\n";
        return 5;
    }
    modeDirectory.setPath(modeDirectoryPath);

    const QByteArray source = deterministicSource();
    const QString sourcePath = fixtureDirectory.filePath(QStringLiteral("source.bin"));
    if (!writeFile(sourcePath, source)) {
        return 6;
    }

    cimbar::Config::update(68);
    EncoderPlus encoder;
    std::vector<std::string> orderedFrames;
    const auto onFrame = [&](const cv::Mat& rgbFrame, unsigned index) {
        const QString filename = QStringLiteral("%1.png").arg(index, 4, 10, QLatin1Char('0'));
        const QString outputPath = modeDirectory.filePath(filename);
        cv::Mat bgrFrame;
        cv::cvtColor(rgbFrame, bgrFrame, cv::COLOR_RGB2BGR);
        if (!cv::imwrite(outputPath.toStdString(), bgrFrame)) {
            return false;
        }
        orderedFrames.push_back(filename.toStdString());
        return true;
    };

    const unsigned generated = encoder.encode_fountain(sourcePath.toStdString(), onFrame, 16, 2.0);
    if (generated != orderedFrames.size()) {
        std::cerr << "encoder callback count mismatch\n";
        return 7;
    }
    if (generated < 3) {
        std::cerr << "fixture needs at least three frames to omit two drop-safe frames; generated "
                  << generated << '\n';
        return 8;
    }

    QJsonArray orderedFramesJson;
    for (const std::string& frame : orderedFrames) {
        orderedFramesJson.append(QString::fromStdString(frame));
    }

    QJsonArray dropSafeFramesJson;
    dropSafeFramesJson.append(QString::fromStdString(orderedFrames.at(0)));
    dropSafeFramesJson.append(QString::fromStdString(orderedFrames.at(1)));

    const QJsonObject manifest{
        {QStringLiteral("sourceSha256"),
         QString::fromLatin1(QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("mode"), 68},
        {QStringLiteral("orderedFrames"), orderedFramesJson},
        {QStringLiteral("dropSafeFrames"), dropSafeFramesJson},
    };
    if (!writeFile(fixtureDirectory.filePath(QStringLiteral("manifest.json")),
                   QJsonDocument(manifest).toJson(QJsonDocument::Indented))) {
        return 9;
    }

    std::cout << "generated " << generated << " frames\n";
    return 0;
}
