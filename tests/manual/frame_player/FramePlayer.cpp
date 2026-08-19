// SPDX-License-Identifier: GPL-3.0-only
#include "FramePlayer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>

namespace {

void setError(QString *destination, const QString &message)
{
    if (destination != nullptr) {
        *destination = message;
    }
}

} // namespace

FramePlayer::FramePlayer(const QString &fixtureDirectory,
                         QString *errorMessage,
                         QWidget *parent)
    : QWidget(parent)
    , display_(new QLabel(this))
    , timer_(new QTimer(this))
{
    setWindowTitle(QStringLiteral("Cimbarpunk Test Frame Player"));
    setAutoFillBackground(true);
    QPalette blackPalette = palette();
    blackPalette.setColor(QPalette::Window, Qt::black);
    setPalette(blackPalette);

    display_->setObjectName(QStringLiteral("frameDisplay"));
    display_->setAlignment(Qt::AlignCenter);
    display_->setStyleSheet(QStringLiteral("background: black"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(display_);

    if (!loadManifest(fixtureDirectory, errorMessage)) {
        return;
    }

    timer_->setInterval(100);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &FramePlayer::advanceFrame);
    showCurrentFrame();
    timer_->start();
    ready_ = true;
}

bool FramePlayer::isReady() const noexcept
{
    return ready_;
}

void FramePlayer::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void FramePlayer::advanceFrame()
{
    currentFrame_ = (currentFrame_ + 1) % frames_.size();
    showCurrentFrame();
}

bool FramePlayer::loadManifest(const QString &fixtureDirectory, QString *errorMessage)
{
    QFile manifest(QDir(fixtureDirectory).filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QStringLiteral("Cannot open fixture manifest: %1").arg(manifest.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QStringLiteral("Invalid fixture manifest: %1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject root = document.object();
    const int mode = root.value(QStringLiteral("mode")).toInt(-1);
    const QJsonArray ordered = root.value(QStringLiteral("orderedFrames")).toArray();
    if (mode < 0 || ordered.isEmpty()) {
        setError(errorMessage, QStringLiteral("Fixture manifest has no playable frames"));
        return false;
    }

    const QDir modeDirectory(QDir(fixtureDirectory).filePath(QStringLiteral("mode%1").arg(mode)));
    for (const QJsonValue &entry : ordered) {
        if (!entry.isString() || entry.toString().isEmpty()) {
            setError(errorMessage, QStringLiteral("Fixture manifest contains an invalid frame name"));
            return false;
        }
        const QString framePath = modeDirectory.absoluteFilePath(entry.toString());
        const QPixmap frame(framePath);
        if (frame.isNull()) {
            setError(errorMessage, QStringLiteral("Cannot load fixture frame: %1").arg(framePath));
            return false;
        }
        frames_.append(framePath);
    }
    return true;
}

void FramePlayer::showCurrentFrame()
{
    const QPixmap frame(frames_.at(currentFrame_));
    display_->setPixmap(frame);
    if (!isVisible() && currentFrame_ == 0) {
        resize(frame.size());
    }
}
