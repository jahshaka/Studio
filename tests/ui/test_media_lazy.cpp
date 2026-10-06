/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.media_lazy — Qt Multimedia is off the startup path
// (STABILITY_PROGRAM_SPEC Lane 6a).
//
// Constructing a QMediaPlayer loads the Qt multimedia (ffmpeg) backend, and
// constructing a QAudioOutput enumerates audio devices — on a Linux desktop
// that is a pipewire connect attempt plus a PulseAudio fallback. Two
// constructors put that on EVERY launch: VideoPreviewWidget's (built eagerly
// by AssetView, which the shell builds unconditionally) and iris::Scene's
// (built by every Scene::create(), i.e. the editor scene, the asset scene and
// every preview scene).
//
// This suite pins both: nothing exists until something is played, playing
// still works, and the not-yet-played state is safe to stop().

#include <QtTest>
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QSignalSpy>

#include "ui/controls/videopreviewwidget.h"
#include "irisgl/document/scenegraph/scene.h"
#include "services/ambienceservice.h"

class MediaLazyTest : public QObject
{
    Q_OBJECT

private slots:
    void videoWidgetHasNoPlayerUntilShown();
    void videoWidgetStopBeforePlayIsSafe();
    void videoWidgetPlaysAfterDeferredConstruction();
    void ambiencePlaysOnlyWhatTheSceneNames();
};

// The regression this whole lane exists for: a freshly constructed preview
// widget must own NO multimedia objects at all.
void MediaLazyTest::videoWidgetHasNoPlayerUntilShown()
{
    VideoPreviewWidget w;
    QCOMPARE(w.findChildren<QMediaPlayer *>().size(), 0);
    QCOMPARE(w.findChildren<QAudioOutput *>().size(), 0);
}

// Page changes call stop() on viewers that were never played; before the
// deferral that was always safe because the player always existed.
void MediaLazyTest::videoWidgetStopBeforePlayIsSafe()
{
    VideoPreviewWidget w;
    w.stop();
    w.stop();
    QCOMPARE(w.findChildren<QMediaPlayer *>().size(), 0);   // still not built
}

// And the deferred wiring must actually work: one showVideo() builds the
// player, binds the audio output and the video sink, and decodes the fixture.
void MediaLazyTest::videoWidgetPlaysAfterDeferredConstruction()
{
    VideoPreviewWidget w;
    w.showVideo(QStringLiteral(JAHSHAKA_TINY_MP4), QStringLiteral("tiny"));

    const auto players = w.findChildren<QMediaPlayer *>();
    QCOMPARE(players.size(), 1);
    QCOMPARE(w.findChildren<QAudioOutput *>().size(), 1);

    QMediaPlayer *player = players.front();
    QVERIFY(player->audioOutput() != nullptr);
    QVERIFY(player->videoSink() != nullptr);

    // tests/scripting/fixtures/tiny.mp4 is 64x64, 16 frames at 8 fps = 2.0 s.
    // durationChanged may already have fired by the time we get here.
    if (player->duration() == 0) {
        QSignalSpy spy(player, &QMediaPlayer::durationChanged);
        QVERIFY2(spy.wait(15000), "the deferred player never reported a duration");
    }
    QVERIFY2(player->duration() > 1500 && player->duration() < 2500,
             qPrintable(QStringLiteral("duration was %1 ms").arg(player->duration())));

    // showVideo() autoplays; stop() must release the source.
    w.stop();
    QCOMPARE(player->source(), QUrl());

    // A second show reuses the same player rather than building another.
    w.showVideo(QStringLiteral(JAHSHAKA_TINY_MP4), QStringLiteral("tiny again"));
    QCOMPARE(w.findChildren<QMediaPlayer *>().size(), 1);
    w.stop();
}

// The world's music (audit D8): the DOCUMENT owns no player any more —
// AmbienceService plays what the scene names. It must still build nothing for
// a world without music, and for a world WITH music it must build a player
// that has a SOURCE and an AUDIO OUTPUT and is PLAYING — the two halves the
// document's old player never had (it played silence). Headless-safe: the
// assertions are the player's state, never a sound.
void MediaLazyTest::ambiencePlaysOnlyWhatTheSceneNames()
{
    QObject owner;
    const QString wav = QStringLiteral(JAHSHAKA_TINY_WAV);
    auto *ambience = new AmbienceService([wav](const QString &guid) {
        return guid == QLatin1String("music-guid") ? wav : QString();
    }, &owner);
    auto scene = iris::Scene::create();
    QVERIFY(!scene.isNull());

    ambience->sync(scene);                 // no music named
    ambience->stop();                      // never played: safe
    QCOMPARE(ambience->findChildren<QMediaPlayer *>().size(), 0);
    QCOMPARE(ambience->state().value("state").toString(), QStringLiteral("stopped"));

    scene->ambientMusicGuid = QStringLiteral("music-guid");
    scene->ambientMusicVolume = 80.0f;
    ambience->sync(scene);
    const QList<QMediaPlayer *> players = ambience->findChildren<QMediaPlayer *>();
    QCOMPARE(players.size(), 1);
    QMediaPlayer *player = players.first();
    QCOMPARE(player->source(), QUrl::fromLocalFile(wav));
    QVERIFY2(player->audioOutput() != nullptr, "the player has an audio output");
    // The media opens asynchronously: settle() is the bounded wait the verb uses.
    ambience->settle(15000);
    QCOMPARE(player->playbackState(), QMediaPlayer::PlayingState);
    QCOMPARE(ambience->state().value("state").toString(), QStringLiteral("playing"));
    QCOMPARE(player->loops(), int(QMediaPlayer::Infinite));
    const float loud = player->audioOutput()->volume();
    QVERIFY(loud > 0.0f && loud <= 1.0f);

    // The volume moves in place: same player, same source, still playing.
    scene->ambientMusicVolume = 20.0f;
    ambience->sync(scene);
    QCOMPARE(ambience->findChildren<QMediaPlayer *>().size(), 1);
    QCOMPARE(player->playbackState(), QMediaPlayer::PlayingState);
    QVERIFY(player->audioOutput()->volume() < loud);

    // A guid with no file plays nothing and says why.
    scene->ambientMusicGuid = QStringLiteral("missing-guid");
    ambience->sync(scene);
    QCOMPARE(player->playbackState(), QMediaPlayer::StoppedState);
    QVERIFY(!ambience->state().value("error").toString().isEmpty());

    // No music again: stopped, source dropped.
    scene->ambientMusicGuid.clear();
    ambience->sync(scene);
    QCOMPARE(player->playbackState(), QMediaPlayer::StoppedState);
    QCOMPARE(player->source(), QUrl());
}

QTEST_MAIN(MediaLazyTest)
#include "test_media_lazy.moc"
