// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "core/SessionTypes.h"

#include <QString>

namespace cimbarpunk {

class IOutputStore {
public:
    virtual ~IOutputStore() = default;
    virtual bool prepareDirectory(const QString& directory, QString* error) = 0;
    virtual OutputResult commit(const DecodedPayload& payload, const QString& directory) = 0;
    virtual void cleanupRegisteredTemporaryFiles() = 0;
};

} // namespace cimbarpunk
