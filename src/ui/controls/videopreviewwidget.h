/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VIDEOPREVIEWWIDGET_H
#define VIDEOPREVIEWWIDGET_H

#include <QImage>
#include <QWidget>

class QAudioOutput;
class QLabel;
class QMediaPlayer;
class QPushButton;
class QSlider;
class QVideoSink;

// The Assets page's video preview (ASSET_MEDIA_SPEC §2): play/pause, seek
// slider, elapsed/total time, loop toggle, volume. showVideo() autoplays
// MUTED (double-clicking a tile must never blast audio); the volume slider
// unmutes.
//
// Frames render through a QVideoSink onto a plain raster canvas instead of
// QVideoWidget: QVideoWidget embeds a native child window, and native
// children inside the stacked pages are exactly the engine-mode (xcb) trap
// CLAUDE.md documents (AA_DontCreateNativeWidgetSiblings / page-mapping
// desync). The sink path is the one already proven headless-safe by the
// thumbnail grabber, and costs ~7 ms per 1080p frame (CPU convert) — fine
// for a preview pane.
class VideoPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoPreviewWidget(QWidget *parent = nullptr);

    void showVideo(const QString &filePath, const QString &displayName);
    void stop();   // selection/page changes stop and release the source
    // (No "is the player up?" accessor: the player is a QObject child of the
    // widget, so ui.media_lazy asks findChild<QMediaPlayer*>() directly —
    // which tests the thing that matters, that no such object EXISTS.)

private:
    // Raster frame canvas: letterboxed, aspect-preserving, no native window.
    class VideoCanvas : public QWidget
    {
    public:
        explicit VideoCanvas(QWidget *parent = nullptr);
        void setFrame(const QImage &image);
        void clear();

    protected:
        void paintEvent(QPaintEvent *event) override;

    private:
        QImage frame;
    };

    // Built on the first showVideo(), not in the constructor: a QMediaPlayer
    // pulls in the ffmpeg backend and a QAudioOutput enumerates audio devices
    // (pipewire/PulseAudio probe). AssetView constructs this widget during
    // shell setup, so eager construction put both on every launch's startup
    // path — STABILITY_PROGRAM_SPEC §1.7c / Lane 6a. nullptr until then.
    void ensurePlayer();
    QMediaPlayer *player = nullptr;
    QAudioOutput *audioOutput = nullptr;
    QVideoSink *videoSink = nullptr;
    VideoCanvas *canvas;
    QLabel *nameLabel;
    QPushButton *playButton;
    QSlider *seekSlider;
    QLabel *timeLabel;
    QPushButton *loopButton;
    QSlider *volumeSlider;
};

#endif // VIDEOPREVIEWWIDGET_H
