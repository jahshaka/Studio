/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/ambienceservice.h"

#include <QAudioOutput>
#include <QEventLoop>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QTimer>
#include <QUrl>
#include <QtMath>

#include "irisgl/document/scenegraph/scene.h"

AmbienceService::AmbienceService(Resolver resolve, QObject *parent)
    : QObject(parent), mResolve(std::move(resolve))
{
}

AmbienceService::~AmbienceService()
{
    // The player goes BEFORE the output it plays into (both are children, but
    // the order of child deletion is not a contract worth leaning on).
    if (mPlayer) mPlayer->stop();
    delete mPlayer;
    mPlayer = nullptr;
    delete mOutput;
    mOutput = nullptr;
}

void AmbienceService::ensurePlayer()
{
    // ON FIRST USE, never in the constructor: building a QMediaPlayer loads
    // the multimedia backend and enumerates the audio devices, which a session
    // whose worlds have no music should never pay for.
    if (mPlayer) return;
    mOutput = new QAudioOutput(this);
    mPlayer = new QMediaPlayer(this);
    mPlayer->setAudioOutput(mOutput);
    // Ambience is a bed under the world, not a track: it loops until the world
    // closes or the music changes.
    mPlayer->setLoops(QMediaPlayer::Infinite);
    mVolume = -1.0f;
}

void AmbienceService::sync(const iris::ScenePtr &scene)
{
    const QString guid = scene ? scene->ambientMusicGuid : QString();
    if (guid.isEmpty()) {
        if (!mGuid.isEmpty()) stop();
        return;
    }

    if (guid != mGuid) {
        // A NEW CHOICE is resolved ONCE, here — never per tick. A guid that
        // does not resolve is remembered with its reason, so a missing file
        // costs one lookup, not one a frame.
        mGuid = guid;
        mError.clear();
        const QString path = mResolve ? mResolve(guid) : QString();
        if (path.isEmpty() || !QFileInfo(path).isFile()) {
            if (mPlayer) {
                mPlayer->stop();
                mPlayer->setSource(QUrl());
            }
            mSource.clear();
            mError = QStringLiteral("the music asset '%1' has no file to play").arg(guid);
            return;
        }
        ensurePlayer();
        mSource = path;
        mPlayer->setSource(QUrl::fromLocalFile(path));
        mPlayer->play();
    }
    if (!mPlayer || mSource.isEmpty()) return;

    // THE VOLUME, applied in place (no restart). The document's 1..100 is a
    // slider position, i.e. a PERCEIVED loudness; the output wants a linear
    // gain, which is what the logarithmic scale conversion is for.
    const float volume = qBound(0.0f, scene->ambientMusicVolume, 100.0f);
    if (volume != mVolume) {
        mVolume = volume;
        mOutput->setVolume(QtAudio::convertVolume(volume / 100.0f,
                                                  QtAudio::LogarithmicVolumeScale,
                                                  QtAudio::LinearVolumeScale));
    }
}

void AmbienceService::settle(int timeoutMs)
{
    if (!mPlayer || mSource.isEmpty()) return;
    const auto settled = [this]() {
        const QMediaPlayer::MediaStatus s = mPlayer->mediaStatus();
        if (s == QMediaPlayer::InvalidMedia || mPlayer->error() != QMediaPlayer::NoError) return true;
        if (s == QMediaPlayer::LoadingMedia || s == QMediaPlayer::NoMedia) return false;
        return mPlayer->playbackState() == QMediaPlayer::PlayingState;
    };
    if (settled()) return;
    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    const auto check = [&]() { if (settled()) loop.quit(); };
    const auto a = connect(mPlayer, &QMediaPlayer::mediaStatusChanged, &loop, check);
    const auto b = connect(mPlayer, &QMediaPlayer::playbackStateChanged, &loop, check);
    const auto c = connect(mPlayer, &QMediaPlayer::errorOccurred, &loop, check);
    deadline.start(timeoutMs);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    disconnect(a);
    disconnect(b);
    disconnect(c);
}

QString AmbienceService::fileFor(const QString &guid) const
{
    return (mResolve && !guid.isEmpty()) ? mResolve(guid) : QString();
}

void AmbienceService::stop()
{
    if (mPlayer) {
        mPlayer->stop();
        mPlayer->setSource(QUrl());
    }
    mGuid.clear();
    mSource.clear();
    mError.clear();
}

QVariantMap AmbienceService::state() const
{
    QString playback = QStringLiteral("stopped");
    if (mPlayer && !mSource.isEmpty()) {
        switch (mPlayer->playbackState()) {
        case QMediaPlayer::PlayingState: playback = QStringLiteral("playing"); break;
        case QMediaPlayer::PausedState:  playback = QStringLiteral("paused"); break;
        case QMediaPlayer::StoppedState: break;
        }
    }
    QVariantMap out;
    out.insert(QStringLiteral("guid"), mGuid);
    out.insert(QStringLiteral("source"), mSource);
    out.insert(QStringLiteral("state"), playback);
    out.insert(QStringLiteral("output"), mOutput != nullptr && mPlayer && mPlayer->audioOutput() == mOutput);
    out.insert(QStringLiteral("gain"), mOutput ? double(mOutput->volume()) : 0.0);
    out.insert(QStringLiteral("error"), !mError.isEmpty() ? mError
                                        : (mPlayer && mPlayer->error() != QMediaPlayer::NoError
                                               ? mPlayer->errorString() : QString()));
    return out;
}
