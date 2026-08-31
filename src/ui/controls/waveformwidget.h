/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H

#include <QWidget>

#include "services/audiopeaks.h"

// The audio preview's waveform strip (ASSET_MEDIA_SPEC §2): custom-painted
// min/max peak envelope in the app's theme (dark ground, #3498db accent),
// a playhead line during playback, and click/drag-to-seek. While the first
// decode of a file runs it shows a "…" progress state.
class WaveformWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget *parent = nullptr);

    void setPeaks(const AudioPeaks::Peaks &peaks);
    void clear();                       // no peaks, no progress text
    void showComputing();               // "…" while the decode runs
    void setDuration(qint64 ms);
    void setPosition(qint64 ms);        // moves the playhead

    QSize sizeHint() const override { return QSize(400, 96); }
    QSize minimumSizeHint() const override { return QSize(120, 48); }

signals:
    void seekRequested(qint64 ms);      // from a click/drag on the waveform

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void seekFromX(int x);

    AudioPeaks::Peaks peaks;
    qint64 durationMs = 0;
    qint64 positionMs = 0;
    bool computing = false;
};

#endif // WAVEFORMWIDGET_H
