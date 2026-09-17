/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef IPLAYERHOST_H
#define IPLAYERHOST_H

// IPlayerHost — the player SPACE, as a seam (verb-coverage audit F1).
//
// The Player page had no verb surface at all: the only way to start it was the
// one play button in PlayerWidget, which meant no script, no test and no MCP
// session could open the space the product ships as its runtime. Adding
// player.* verbs that called PlayerWidget would have put the capability in a
// widget, so the verbs go through PlayerService (services/playerservice.h),
// which drives THIS — exactly the arrangement editor.play has with
// PlaybackService and IEditorViewport.
//
// EnginePlayerView is the one implementation. Headless runs have none: the
// service holds a null host and every verb refuses cleanly, which is the same
// contract every other service in this layer has.

#include <QImage>
#include <QString>
#include <QVariantMap>

#include "viewport/flystep.h"

class IPlayerHost
{
public:
    virtual ~IPlayerHost() = default;

    /// Is the scene RUNNING (PlayBack driving physics/animation/controllers)?
    virtual bool isScenePlaying() = 0;
    virtual void playScene() = 0;
    virtual void stopScene() = 0;

    /// Is the player page the visible one? The player only steps and renders
    /// while it is (EnginePlayerView::start/end), so this is the difference
    /// between "playing" and "playing where anyone can see it".
    virtual bool isPlayerActive() const = 0;

    /// The player's own offscreen readback (EnginePlayerScene::takeScreenshot).
    /// Null QImage when the player has no engine scene or no camera.
    /// `grade` is IEditorViewport::ScreenshotGrade as an int.
    virtual QImage takePlayerScreenshot(int width, int height, int grade) = 0;

    /// Steps and renders exactly n player frames synchronously; `dt` < 0 uses
    /// the wall clock. The deterministic stepping player assertions need,
    /// mirroring editor.frame. FALSE when there is nothing to step — the
    /// player's on-screen View is created by its show event, so a page that
    /// has never been shown cannot render a frame, and saying so is better
    /// than answering true and drawing nothing.
    virtual bool stepPlayerFrames(int n, float dt) = 0;

    // ---- THE PLAYER'S VR MODE (SPECS/VR_SPEC.md §4.5, phase 3) -------------
    // The same seam, for the same reason: `player.play({vr:true})` and the VR
    // icon must reach ONE implementation, and a headless session (no player
    // backend at all) must refuse them in words rather than crash.

    /// Starts the player's VR session: the headset shows this player's scene,
    /// this player's on-screen View becomes the mirror, and the rig is placed
    /// on the play camera. False with `error` filled and NOTHING changed.
    virtual bool beginPlayerVr(const QVariantMap &options, QString *error) = 0;
    /// Ends it. Safe when none is running.
    virtual void endPlayerVr() = 0;
    /// Is the ENGINE running the session this player started?
    virtual bool isPlayerVrActive() const = 0;
    /// Everything player.state().vr reports. Answerable with no session and
    /// with no runtime (every field at its zero).
    virtual QVariantMap playerVrReport() const = 0;
    /// Moves the wearer as the fly keys would, for `seconds` at the player's
    /// fly speed. False when no session is running.
    virtual bool movePlayerVr(const flystep::Keys &keys, float seconds) = 0;
    /// "I am standing here, facing this way": re-places the rig on the play
    /// camera at the next located pose. False when no session is running.
    virtual bool recenterPlayerVr() = 0;
};

#endif // IPLAYERHOST_H
