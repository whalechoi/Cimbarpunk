// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "pipeline/IFrameProcessor.h"

#include <QStringList>

namespace cimbarpunk::test {

class FakeFrameProcessor final : public IFrameProcessor {
    Q_OBJECT

public:
    explicit FakeFrameProcessor(QStringList* events = nullptr, QObject* parent = nullptr)
        : IFrameProcessor(parent)
        , m_events(events) {
    }

    bool start(const ScreenSelection& selection, const QString& outputDirectory, QString* error) override {
        ++startCalls;
        lastSelection = selection;
        lastOutputDirectory = outputDirectory;
        record(QStringLiteral("processor.start"));
        if (!startSucceeds) {
            if (error != nullptr) {
                *error = startError;
            }
            return false;
        }

        running = true;
        return true;
    }

    void submitFrame(const QImage& frame) override {
        submittedFrames.append(frame);
        record(QStringLiteral("processor.submit"));
    }

    void stop() override {
        ++stopCalls;
        record(QStringLiteral("processor.stop"));
        running = false;
    }

    void acceptFrame() {
        ++acceptedFrames;
        emit frameAccepted();
    }

    void reportProgress(double progress) {
        emit progressChanged(progress);
    }

    void succeed(const OutputResult& result) {
        emit completed(result);
    }

    void reportFailure(const QString& message) {
        emit failed(message);
    }

    bool startSucceeds = true;
    QString startError = QStringLiteral("processor start failed");
    int startCalls = 0;
    int stopCalls = 0;
    int acceptedFrames = 0;
    bool running = false;
    ScreenSelection lastSelection;
    QString lastOutputDirectory;
    QList<QImage> submittedFrames;

private:
    void record(const QString& event) {
        if (m_events != nullptr) {
            m_events->append(event);
        }
    }

    QStringList* m_events;
};

} // namespace cimbarpunk::test
