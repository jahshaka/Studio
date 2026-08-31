/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VIDEOUTILS_H
#define VIDEOUTILS_H

#include <QImage>
#include <QJsonObject>
#include <QString>

// Video probing and frame grabbing through Qt Multimedia's ffmpeg backend
// (ASSET_MEDIA_SPEC.md §0/§1) — no vendored decoder, QMediaPlayer does the
// work. Both calls spin a local QEventLoop until the player reports in, so
// they are synchronous to the caller but only legal on the GUI thread
// (Qt Multimedia objects want the thread with the event dispatcher that
// owns them). canUseMultimedia() is the guard; off the GUI thread both
// calls return empty results instead of misbehaving.
//
// Verified headless-safe: the ffmpeg plugin decodes and QVideoFrame
// converts (CPU path) under QT_QPA_PLATFORM=offscreen — no display needed.
class VideoUtils
{
public:
    // True when a Q(Gui)Application exists and we are on its thread.
    static bool canUseMultimedia();

    // {duration (ms), width, height, frameRate, videoCodec} — whatever the
    // container reports; empty object on failure/timeout/wrong thread.
    static QJsonObject probeFile(const QString &filePath, int timeoutMs = 8000);

    // First decodable frame at/after positionMs (clamped into the stream;
    // "first second" thumbnails pass 500). Null image on failure.
    static QImage grabFrame(const QString &filePath, qint64 positionMs = 500,
                            int timeoutMs = 8000);

    // The 256x256-bounded tile thumbnail: the grabbed frame, or the film
    // icon fallback when decode fails (ASSET_MEDIA_SPEC §1).
    static QPixmap thumbnailFor(const QString &filePath);
};

#endif // VIDEOUTILS_H
