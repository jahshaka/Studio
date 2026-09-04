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

class MediaLazyTest : public QObject
{
    Q_OBJECT

private slots:
    void videoWidgetHasNoPlayerUntilShown();
    void videoWidgetStopBeforePlayIsSafe();
    void videoWidgetPlaysAfterDeferredConstruction();
    void sceneHasNoAmbientPlayerUntilPlayed();
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

// The document half: iris::Scene's ambient-music player. It is parentless, so
// there is nothing to findChild for — the observable contract is that a Scene
// that never played music can be told to stop and does not crash, which is
// exactly what the nullptr guard buys.
void MediaLazyTest::sceneHasNoAmbientPlayerUntilPlayed()
{
    auto scene = iris::Scene::create();
    QVERIFY(!scene.isNull());
    scene->stopPlayingAmbientMusic();   // would have been a null deref
    scene->setAmbientMusicVolume(25);
    scene->stopPlayingAmbientMusic();
}

QTEST_MAIN(MediaLazyTest)
#include "test_media_lazy.moc"
