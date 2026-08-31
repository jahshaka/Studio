/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "videoutils.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QPixmap>
#include <QThread>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

#include "irisgl/core/irisutils.h"

bool VideoUtils::canUseMultimedia()
{
    return QCoreApplication::instance()
           && QThread::currentThread() == QCoreApplication::instance()->thread();
}

QJsonObject VideoUtils::probeFile(const QString &filePath, int timeoutMs)
{
    if (!canUseMultimedia() || !QFileInfo::exists(filePath)) return QJsonObject();

    QMediaPlayer player;
    QEventLoop loop;
    bool ok = false;

    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &loop,
                     [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
            ok = true;
            loop.quit();
        }
        else if (status == QMediaPlayer::InvalidMedia) {
            loop.quit();
        }
    });
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop, [&]() { loop.quit(); });

    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    player.setSource(QUrl::fromLocalFile(filePath));
    loop.exec();
    if (!ok) return QJsonObject();

    QJsonObject meta;
    if (player.duration() > 0) meta["duration"] = player.duration();

    const QMediaMetaData md = player.metaData();
    const QSize resolution = md.value(QMediaMetaData::Resolution).toSize();
    if (resolution.isValid() && !resolution.isEmpty()) {
        meta["width"] = resolution.width();
        meta["height"] = resolution.height();
    }
    const qreal fps = md.value(QMediaMetaData::VideoFrameRate).toReal();
    if (fps > 0.0) meta["frameRate"] = fps;
    const QString codec = md.stringValue(QMediaMetaData::VideoCodec);
    if (!codec.isEmpty()) meta["videoCodec"] = codec;

    return meta;
}

QImage VideoUtils::grabFrame(const QString &filePath, qint64 positionMs, int timeoutMs)
{
    if (!canUseMultimedia() || !QFileInfo::exists(filePath)) return QImage();

    QMediaPlayer player;
    QVideoSink sink;
    player.setVideoSink(&sink);

    QEventLoop loop;
    QImage frameImage;

    QObject::connect(&sink, &QVideoSink::videoFrameChanged, &loop,
                     [&](const QVideoFrame &frame) {
        if (!frame.isValid() || !frameImage.isNull()) return;
        const QImage img = frame.toImage();
        if (!img.isNull()) {
            frameImage = img;
            loop.quit();
        }
    });
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &loop,
                     [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia) {
            // Clamp into the stream; very short clips just play from 0.
            const qint64 duration = player.duration();
            if (duration > 0 && positionMs > 0)
                player.setPosition(qMin(positionMs, duration - 1));
            player.play();
        }
        else if (status == QMediaPlayer::InvalidMedia
                 || status == QMediaPlayer::EndOfMedia) {
            loop.quit();
        }
    });
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &loop, [&]() { loop.quit(); });

    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    player.setSource(QUrl::fromLocalFile(filePath));
    loop.exec();
    player.stop();
    return frameImage;
}

QPixmap VideoUtils::thumbnailFor(const QString &filePath)
{
    const QImage frame = grabFrame(filePath);
    if (!frame.isNull()) {
        const QImage scaled = frame.width() >= frame.height()
                                  ? frame.scaledToWidth(256, Qt::SmoothTransformation)
                                  : frame.scaledToHeight(256, Qt::SmoothTransformation);
        return QPixmap::fromImage(scaled);
    }
    return QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-video.png"));
}
