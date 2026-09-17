/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/playerservice.h"

#include "player/iplayerhost.h"

bool PlayerService::isPlaying()
{
    return mHost && mHost->isScenePlaying();
}

bool PlayerService::isActive() const
{
    return mHost && mHost->isPlayerActive();
}

bool PlayerService::play(bool vr)
{
    if (!mHost) return false;
    mLastError.clear();
    // VR FIRST, AND IT IS A VETO (SPECS/VR_SPEC.md §4.5). A caller that asked
    // for the headset and got a flat player instead has been lied to, and a
    // script or an MCP session cannot tell the difference — so a box with no
    // runtime REFUSES and nothing moves: the scene does not start, the page
    // does not change state, and the reason is in words. Wanting "VR if there
    // is any" is two calls (vrAvailable() then play), which is the honest
    // shape of a question with two answers.
    //
    // THE SESSION IS STARTED BEFORE THE SCENE RUNS because the rig is placed on
    // the play camera, and the play camera is where it stands NOW — a session
    // begun after a second of physics would stand the wearer wherever the scene
    // had already carried the camera to.
    if (vr && !mHost->isPlayerVrActive()) {
        QString error;
        if (!mHost->beginPlayerVr(QVariantMap(), &error)) {
            mLastError = error;
            return false;
        }
    }
    if (mHost->isScenePlaying()) return true;
    mHost->playScene();
    const bool now = mHost->isScenePlaying();
    if (now) emit playingChanged(true);
    return now;
}

bool PlayerService::stop()
{
    if (!mHost) return false;
    // STOPPING THE PLAYER TAKES THE HEADSET OFF (the order chosen for VR_SPEC
    // §4.5): a session is a property of a RUN — it mirrors the player's view,
    // it switches that view off, it paces the whole render loop from the
    // runtime's clock — and leaving one running over a stopped world would
    // leave the wearer standing in a frozen scene with the desktop showing a
    // still of it. The other order is `player.endVr()` / `vr.end()`, which ends
    // the session and leaves the scene playing.
    mHost->endPlayerVr();
    if (!mHost->isScenePlaying()) return true;
    mHost->stopScene();
    const bool now = mHost->isScenePlaying();
    if (!now) emit playingChanged(false);
    return !now;
}

bool PlayerService::vrAvailable() const
{
    return mHost && mHost->playerVrReport().value(QStringLiteral("available")).toBool();
}

QString PlayerService::vrUnavailableReason() const
{
    if (!mHost)
        return QStringLiteral("this session has no player backend");
    return mHost->playerVrReport().value(QStringLiteral("reason")).toString();
}

bool PlayerService::isVrActive() const { return mHost && mHost->isPlayerVrActive(); }

bool PlayerService::endVr()
{
    if (!mHost || !mHost->isPlayerVrActive()) return false;
    mHost->endPlayerVr();
    return true;
}

bool PlayerService::toggleVr()
{
    if (!mHost) return false;
    if (isVrActive()) {
        // A SECOND PRESS STOPS THE RUN, not just the session: the toggle's
        // promise is "take me in / take me out", and a wearer who has taken the
        // headset off does not want the world still running behind a page they
        // are no longer looking at. `player.endVr()` is the other order, for a
        // caller that means exactly that.
        stop();
        return false;
    }
    // AVAILABILITY BEFORE ANYTHING MOVES. Showing the Player page START THE
    // SCENE (switchSpace(PLAYER) plays it, by design since audit F4), so a
    // toggle that opened the page and only then discovered there was no
    // runtime would leave the user in a running Player they never asked for —
    // the worst possible answer to "put me in VR" on a box with no headset.
    if (!vrAvailable()) {
        mLastError = vrUnavailableReason();
        return false;
    }
    // THE PAGE NEXT. The mirror is the Player's on-screen View and that View
    // is created by the page's show event, so a toggle from the editor has to
    // open the page before there is anything to mirror onto.
    showSpace();
    return play(true);
}

bool PlayerService::moveVr(const flystep::Keys &keys, float seconds)
{
    return mHost && mHost->movePlayerVr(keys, seconds);
}

bool PlayerService::recenterVr() { return mHost && mHost->recenterPlayerVr(); }

QVariantMap PlayerService::vrReport() const
{
    if (mHost) return mHost->playerVrReport();
    // No player backend at all (a headless run): the honest answer to every
    // field, without reaching for a VR class this layer would otherwise not
    // need to know about.
    return QVariantMap{ { QStringLiteral("active"), false },
                        { QStringLiteral("available"), false },
                        { QStringLiteral("state"), QStringLiteral("unavailable") },
                        { QStringLiteral("reason"),
                          QStringLiteral("this session has no player backend") } };
}

bool PlayerService::showSpace()
{
    if (!mShowSpace) return false;
    mShowSpace();
    return true;
}

bool PlayerService::restart()
{
    if (!mHost) return false;
    // Stop THEN play, always — a restart of a stopped player is just a play,
    // and a restart of a running one has to go through the stop so PlayBack
    // puts the scene's pre-play transforms back before it runs again.
    if (mHost->isScenePlaying()) mHost->stopScene();
    mHost->playScene();
    const bool now = mHost->isScenePlaying();
    emit playingChanged(now);
    return now;
}

QImage PlayerService::screenshot(int width, int height, int grade)
{
    if (!mHost) return QImage();
    return mHost->takePlayerScreenshot(width, height, grade);
}

bool PlayerService::stepFrames(int n, float dt)
{
    return mHost && mHost->stepPlayerFrames(n, dt);
}
