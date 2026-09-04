/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "videopreviewwidget.h"

#include <QAudioOutput>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaPlayer>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

namespace {
QString formatTime(qint64 ms)
{
    const qint64 secs = ms / 1000;
    return QStringLiteral("%1:%2").arg(secs / 60).arg(secs % 60, 2, 10, QChar('0'));
}
} // namespace

VideoPreviewWidget::VideoCanvas::VideoCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(160, 90);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VideoPreviewWidget::VideoCanvas::setFrame(const QImage &image)
{
    frame = image;
    update();
}

void VideoPreviewWidget::VideoCanvas::clear()
{
    frame = QImage();
    update();
}

void VideoPreviewWidget::VideoCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x11, 0x11, 0x11));
    if (frame.isNull()) return;
    QSize target = frame.size();
    target.scale(size(), Qt::KeepAspectRatio);
    QRect dest(QPoint(0, 0), target);
    dest.moveCenter(rect().center());
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(dest, frame);
}

VideoPreviewWidget::VideoPreviewWidget(QWidget *parent) : QWidget(parent)
{
    // player / audioOutput / videoSink are deferred to ensurePlayer(); see .h.
    canvas = new VideoCanvas;

    nameLabel = new QLabel;
    nameLabel->setAlignment(Qt::AlignCenter);
    nameLabel->setStyleSheet("font-size: 14px; color: #EEEEEE; padding: 4px;");

    playButton = new QPushButton(tr("Pause"));
    playButton->setFixedWidth(64);
    playButton->setCursor(Qt::PointingHandCursor);

    seekSlider = new QSlider(Qt::Horizontal);
    seekSlider->setRange(0, 0);

    timeLabel = new QLabel("0:00 / 0:00");
    timeLabel->setStyleSheet("color: #BABABA;");

    loopButton = new QPushButton(tr("Loop"));
    loopButton->setCheckable(true);
    loopButton->setFixedWidth(56);
    loopButton->setCursor(Qt::PointingHandCursor);
    loopButton->setToolTip(tr("Loop playback"));

    volumeSlider = new QSlider(Qt::Horizontal);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(0);   // autoplay is muted; slide up to hear it
    volumeSlider->setFixedWidth(80);
    volumeSlider->setToolTip(tr("Volume"));

    auto controls = new QHBoxLayout;
    controls->setContentsMargins(12, 6, 12, 8);
    controls->addWidget(playButton);
    controls->addWidget(seekSlider);
    controls->addWidget(timeLabel);
    controls->addWidget(loopButton);
    controls->addWidget(volumeSlider);

    auto layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(nameLabel);
    layout->addWidget(canvas, 1);
    layout->addLayout(controls);
    setLayout(layout);

    // Widget-only wiring. Everything that dereferences `player` either goes
    // through ensurePlayer() first or nullptr-guards: the controls are visible
    // before any video has been selected, and clicking them then must not
    // spin up a decoder for nothing.
    connect(playButton, &QPushButton::clicked, this, [this]() {
        if (!player) return;   // nothing loaded yet
        if (player->playbackState() == QMediaPlayer::PlayingState) player->pause();
        else player->play();
    });
    connect(seekSlider, &QSlider::sliderMoved, this, [this](int position) {
        if (player) player->setPosition(position);
    });
    connect(loopButton, &QPushButton::toggled, this, [this](bool looping) {
        if (player) player->setLoops(looping ? QMediaPlayer::Infinite : 1);
    });
    connect(volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        if (!audioOutput) return;
        audioOutput->setMuted(value == 0);
        audioOutput->setVolume(value / 100.0f);
    });
}

// See the note on the members: the decoder and the audio device are created on
// the first video the user actually asks for, never at construction.
void VideoPreviewWidget::ensurePlayer()
{
    if (player) return;

    player = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    player->setAudioOutput(audioOutput);

    // Raster sink path — no QVideoWidget, no native child window (see .h).
    videoSink = new QVideoSink(this);
    player->setVideoSink(videoSink);
    connect(videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame) {
        if (frame.isValid()) canvas->setFrame(frame.toImage());
    });

    connect(player, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
        playButton->setText(state == QMediaPlayer::PlayingState ? tr("Pause") : tr("Play"));
    });
    connect(player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        seekSlider->setRange(0, static_cast<int>(duration));
    });
    connect(player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (!seekSlider->isSliderDown())
            seekSlider->setValue(static_cast<int>(position));
        timeLabel->setText(formatTime(position) + " / " + formatTime(player->duration()));
    });
}

void VideoPreviewWidget::showVideo(const QString &filePath, const QString &displayName)
{
    ensurePlayer();
    nameLabel->setText(displayName);
    seekSlider->setValue(0);
    canvas->clear();
    audioOutput->setMuted(volumeSlider->value() == 0);
    audioOutput->setVolume(volumeSlider->value() / 100.0f);
    player->setLoops(loopButton->isChecked() ? QMediaPlayer::Infinite : 1);
    player->setSource(QUrl::fromLocalFile(filePath));
    player->play();   // autoplay muted on select (ASSET_MEDIA_SPEC §2)
}

void VideoPreviewWidget::stop()
{
    if (!player) return;         // never played anything: nothing to release
    player->stop();
    player->setSource(QUrl());   // release the file handle/decoder
    canvas->clear();
}
