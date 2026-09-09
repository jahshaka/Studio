/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/livevideo.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QMap>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include "services/livetextures.h"

namespace {

QMap<QString, LiveVideoBinding *> &bindings()
{
    static QMap<QString, LiveVideoBinding *> table;
    return table;
}

}

LiveVideoBinding::LiveVideoBinding(const QString &videoGuid_, const QString &textureGuid_,
                                   const QString &filePath, QObject *parent)
    : QObject(parent), videoGuid(videoGuid_), liveGuid(textureGuid_), source(filePath)
{
    player = new QMediaPlayer(this);
    sink   = new QVideoSink(this);
    // NO QAudioOutput. A video bound to a texture is a MATERIAL, not a
    // playback session: attaching one would enumerate audio devices (the
    // pipewire probe behind the ui.media_lazy flake) and blast sound the
    // moment a material started animating.
    player->setVideoSink(sink);
    connect(sink, &QVideoSink::videoFrameChanged, this, &LiveVideoBinding::onFrame);
    player->setSource(QUrl::fromLocalFile(filePath));
}

void LiveVideoBinding::onFrame(const QVideoFrame &frame)
{
    if (!frame.isValid()) return;
    QImage image = frame.toImage();
    if (image.isNull()) return;
    const QVariantMap info = LiveTextureCatalog::info(liveGuid);
    if (info.isEmpty()) return;   // the texture was destroyed under us
    const int w = info.value("width").toInt();
    const int h = info.value("height").toInt();
    // SCALED TO THE TEXTURE, not the other way round: a live texture's size is
    // fixed at creation (the renderer's texture cannot resize), and a video's
    // resolution is whatever the file says. IgnoreAspectRatio because the
    // texture's aspect is the material author's decision, not the clip's — a
    // letterbox baked into a base-colour map would be a surprise nobody asked
    // for.
    if (image.width() != w || image.height() != h)
        image = image.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (image.format() != QImage::Format_RGBA8888)
        image = image.convertToFormat(QImage::Format_RGBA8888);
    if (!LiveTextureCatalog::write(liveGuid, image, &lastError)) return;
    ++frameCount;
    emit frameDelivered();
}

bool LiveVideoBinding::play()
{
    player->play();
    return true;
}

bool LiveVideoBinding::pause()
{
    player->pause();
    return true;
}

bool LiveVideoBinding::stop()
{
    player->stop();
    return true;
}

bool LiveVideoBinding::seek(qint64 positionMs)
{
    if (positionMs < 0) return false;
    player->setPosition(positionMs);
    return true;
}

void LiveVideoBinding::setLoop(bool loop)
{
    looping = loop;
    player->setLoops(loop ? QMediaPlayer::Infinite : 1);
}

bool LiveVideoBinding::step(int timeoutMs)
{
    const quint64 before = frameCount;
    const bool wasPlaying = player->playbackState() == QMediaPlayer::PlayingState;
    if (!wasPlaying) player->play();

    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(this, &LiveVideoBinding::frameDelivered, &loop, &QEventLoop::quit);
    // A clip that ENDED delivers nothing ever again; without this the step
    // would sit out its whole timeout at the tail of every non-looping video.
    connect(player, &QMediaPlayer::mediaStatusChanged, &loop,
            [&loop](QMediaPlayer::MediaStatus s) {
                if (s == QMediaPlayer::EndOfMedia || s == QMediaPlayer::InvalidMedia) loop.quit();
            });
    deadline.start(timeoutMs > 0 ? timeoutMs : 4000);
    loop.exec();

    if (!wasPlaying) player->pause();
    return frameCount > before;
}

QVariantMap LiveVideoBinding::state() const
{
    QVariantMap out;
    out["video"] = videoGuid;
    out["texture"] = liveGuid;
    out["source"] = source;
    out["playing"] = player->playbackState() == QMediaPlayer::PlayingState;
    out["paused"] = player->playbackState() == QMediaPlayer::PausedState;
    out["position"] = double(player->position());
    out["duration"] = double(player->duration());
    out["loop"] = looping;
    out["frames"] = double(frameCount);
    if (!lastError.isEmpty()) out["lastError"] = lastError;
    return out;
}

LiveVideoBinding *LiveVideo::bind(const QString &videoGuid, const QString &textureGuid,
                                  const QString &filePath, QString *error)
{
    auto refuse = [error](const QString &message) -> LiveVideoBinding * {
        if (error) *error = message;
        return nullptr;
    };
    if (videoGuid.isEmpty()) return refuse(QStringLiteral("no video guid"));
    if (!LiveTextureCatalog::exists(textureGuid))
        return refuse(QStringLiteral("no live texture '%1' (texture.createLive makes one)")
                          .arg(textureGuid));
    if (!QFileInfo::exists(filePath))
        return refuse(QStringLiteral("the video file '%1' does not exist").arg(filePath));

    // ONE SHUTDOWN HOOK, armed by the first bind. A QMediaPlayer that outlives
    // QCoreApplication takes the ffmpeg backend down with it at exit — the
    // class of crash that only ever shows up in a suite's exit code.
    static bool hooked = false;
    if (!hooked && QCoreApplication::instance()) {
        hooked = true;
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                         QCoreApplication::instance(), [] { LiveVideo::clear(); });
    }

    unbind(videoGuid);
    auto *binding = new LiveVideoBinding(videoGuid, textureGuid, filePath);
    bindings().insert(videoGuid, binding);
    return binding;
}

bool LiveVideo::unbind(const QString &videoGuid)
{
    auto it = bindings().find(videoGuid);
    if (it == bindings().end()) return false;
    LiveVideoBinding *binding = it.value();
    bindings().erase(it);
    // Stop first: deleteLater on a still-decoding player leaves the backend
    // pushing frames at a sink that is about to die.
    binding->stop();
    delete binding;
    return true;
}

LiveVideoBinding *LiveVideo::find(const QString &videoGuid)
{
    return bindings().value(videoGuid, nullptr);
}

QStringList LiveVideo::bound()
{
    return bindings().keys();
}

void LiveVideo::clear()
{
    const QStringList guids = bindings().keys();
    for (const QString &guid : guids) unbind(guid);
}
