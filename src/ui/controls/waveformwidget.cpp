/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "waveformwidget.h"

#include <QMouseEvent>
#include <QPainter>

namespace {
const QColor GroundColor(0x1A, 0x1A, 0x1A);
const QColor FrameColor(0x40, 0x40, 0x40);
const QColor WaveColor(0x34, 0x98, 0xdb);          // the app accent
const QColor WavePlayedColor(0x7f, 0xc4, 0xf2);    // brighter: already played
const QColor CenterLineColor(0x30, 0x30, 0x30);
const QColor PlayheadColor(0xEE, 0xEE, 0xEE);
const QColor TextColor(0xBA, 0xBA, 0xBA);
} // namespace

WaveformWidget::WaveformWidget(QWidget *parent) : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void WaveformWidget::setPeaks(const AudioPeaks::Peaks &newPeaks)
{
    peaks = newPeaks;
    computing = false;
    update();
}

void WaveformWidget::clear()
{
    peaks = AudioPeaks::Peaks();
    computing = false;
    durationMs = 0;
    positionMs = 0;
    update();
}

void WaveformWidget::showComputing()
{
    peaks = AudioPeaks::Peaks();
    computing = true;
    update();
}

void WaveformWidget::setDuration(qint64 ms)
{
    durationMs = ms;
    update();
}

void WaveformWidget::setPosition(qint64 ms)
{
    positionMs = ms;
    update();
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRectF r = rect();
    p.fillRect(r, GroundColor);
    p.setPen(FrameColor);
    p.drawRect(r.adjusted(0, 0, -1, -1));

    if (computing) {
        p.setPen(TextColor);
        p.drawText(r, Qt::AlignCenter, QStringLiteral("…"));
        return;
    }
    if (peaks.isEmpty()) return;

    const qreal midY = r.height() / 2.0;
    const qreal halfH = midY - 4.0;
    const int buckets = peaks.buckets();
    const qreal playedX = durationMs > 0
        ? r.width() * qBound<qreal>(0.0, qreal(positionMs) / qreal(durationMs), 1.0)
        : -1.0;

    p.setPen(CenterLineColor);
    p.drawLine(QPointF(1, midY), QPointF(r.width() - 2, midY));

    // One vertical min→max bar per pixel column, sampled from the buckets.
    const int w = qMax(1, int(r.width()) - 2);
    for (int x = 0; x < w; ++x) {
        const int b0 = int(qint64(x) * buckets / w);
        const int b1 = qMax(b0 + 1, int(qint64(x + 1) * buckets / w));
        int lo = 127, hi = -127;
        for (int b = b0; b < b1 && b < buckets; ++b) {
            lo = qMin<int>(lo, peaks.mins[b]);
            hi = qMax<int>(hi, peaks.maxs[b]);
        }
        if (lo > hi) continue;
        const qreal yTop = midY - (hi / 127.0) * halfH;
        const qreal yBottom = midY - (lo / 127.0) * halfH;
        p.setPen(x + 1 <= playedX ? WavePlayedColor : WaveColor);
        p.drawLine(QPointF(x + 1, yTop), QPointF(x + 1, qMax(yBottom, yTop + 1.0)));
    }

    if (playedX >= 0.0) {
        p.setPen(QPen(PlayheadColor, 1));
        p.drawLine(QPointF(playedX, 1), QPointF(playedX, r.height() - 2));
    }
}

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) seekFromX(event->pos().x());
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton) seekFromX(event->pos().x());
}

void WaveformWidget::seekFromX(int x)
{
    if (durationMs <= 0 || width() <= 0) return;
    const qreal t = qBound<qreal>(0.0, qreal(x) / qreal(width()), 1.0);
    const qint64 ms = qint64(t * durationMs);
    positionMs = ms;   // immediate visual feedback; the player echoes it back
    update();
    emit seekRequested(ms);
}
