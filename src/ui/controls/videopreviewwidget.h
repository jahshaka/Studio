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

    QMediaPlayer *player;
    QAudioOutput *audioOutput;
    QVideoSink *videoSink;
    VideoCanvas *canvas;
    QLabel *nameLabel;
    QPushButton *playButton;
    QSlider *seekSlider;
    QLabel *timeLabel;
    QPushButton *loopButton;
    QSlider *volumeSlider;
};

#endif // VIDEOPREVIEWWIDGET_H
