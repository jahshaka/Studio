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

bool PlayerService::play()
{
    if (!mHost) return false;
    if (mHost->isScenePlaying()) return true;
    mHost->playScene();
    const bool now = mHost->isScenePlaying();
    if (now) emit playingChanged(true);
    return now;
}

bool PlayerService::stop()
{
    if (!mHost) return false;
    if (!mHost->isScenePlaying()) return true;
    mHost->stopScene();
    const bool now = mHost->isScenePlaying();
    if (!now) emit playingChanged(false);
    return !now;
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
