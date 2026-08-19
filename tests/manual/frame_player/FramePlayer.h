// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>

#include <QStringList>

class QLabel;
class QKeyEvent;
class QTimer;

class FramePlayer final : public QWidget
{
    Q_OBJECT

public:
    explicit FramePlayer(const QString &fixtureDirectory,
                         QString *errorMessage = nullptr,
                         QWidget *parent = nullptr);

    [[nodiscard]] bool isReady() const noexcept;

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void advanceFrame();

private:
    bool loadManifest(const QString &fixtureDirectory, QString *errorMessage);
    void showCurrentFrame();

    QLabel *display_ = nullptr;
    QTimer *timer_ = nullptr;
    QStringList frames_;
    qsizetype currentFrame_ = 0;
    bool ready_ = false;
};
