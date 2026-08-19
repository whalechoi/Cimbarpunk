// SPDX-License-Identifier: GPL-3.0-only
#include "output/LibcimbarPayloadWriter.h"

#include "compression/zstd_decompressor.h"
#include "zstd/zstd.h"

#include <filesystem>
#include <fstream>

namespace cimbarpunk {

PayloadWriter makeLibcimbarPayloadWriter() {
    return [](const QString& temporaryPath, const QByteArrayView compressedBytes, QString* error) {
        if (compressedBytes.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("Compressed payload is empty");
            }
            return false;
        }

        const size_t frameSize = ZSTD_findFrameCompressedSize(compressedBytes.data(), static_cast<size_t>(compressedBytes.size()));
        if (ZSTD_isError(frameSize) || frameSize != static_cast<size_t>(compressedBytes.size())) {
            if (error != nullptr) {
                *error = ZSTD_isError(frameSize)
                    ? QStringLiteral("Invalid compressed payload: %1").arg(QString::fromUtf8(ZSTD_getErrorName(frameSize)))
                    : QStringLiteral("Compressed payload contains trailing or incomplete frame data");
            }
            return false;
        }

        const std::filesystem::path path(temporaryPath.toStdWString());
        cimbar::zstd_decompressor<std::ofstream> output(path, std::ios::out | std::ios::binary);
        const bool wrote = output.write(compressedBytes.data(), static_cast<size_t>(compressedBytes.size()));
        output.flush();
        const bool wroteSuccessfully = wrote && output.good();
        output.close();
        if (!wroteSuccessfully || !output.good()) {
            if (error != nullptr) {
                *error = QString::fromStdString(output.last_error());
                if (error->isEmpty()) {
                    *error = QStringLiteral("Unable to decompress payload to temporary file");
                }
            }
            return false;
        }
        return true;
    };
}

} // namespace cimbarpunk
