/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/playbackservice.h"

#include "viewport/ieditorviewport.h"

bool PlaybackService::isPlaying() const
{
    return viewport ? viewport->isPlaying() : playing;
}

void PlaybackService::enterEditMode()
{
    playing = false;
    mode = SceneMode::EditMode;
    // The viewport's own flag is the one its event handlers branch on: without
    // this, entering the editor after play-in-place left mPlaying true and the
    // viewport routed EVERY mouse and key event to the player controller —
    // which has no selection path — forever ("can't click anything in a loaded
    // scene", 2026-09-05). Every other transition in this class already drives
    // the viewport; this was the one that didn't.
    if (viewport) viewport->stopPlayingScene();
    emit editModeEntered();
}

void PlaybackService::enterPlayMode()
{
    playing = true;
    mode = SceneMode::PlayMode;
    // Deliberately NOT symmetric: no startPlayingScene() here. switchSpace's
    // PLAYER case starts the player page's own playback; starting play-in-place
    // on the EDITOR viewport as well would have two drivers on one document.
    emit playModeEntered();
}

void PlaybackService::playScene()
{
    playing = true;
    mode = SceneMode::PlayMode;
    if (viewport) viewport->startPlayingScene();
}

// `playing` means "running", not "in play mode": a paused scene stays in
// PlayMode with playing == false, and the next playScene() RESUMES it (the
// viewport hands the resume to PlayBack, which keeps the physics world and the
// pre-play transforms). Only stopScene() leaves play mode's state behind.
void PlaybackService::pauseScene()
{
    playing = false;
    mode = SceneMode::PlayMode;
    if (viewport) viewport->pausePlayingScene();
}

void PlaybackService::restartScene()
{
    playing = true;
    mode = SceneMode::PlayMode;
    if (viewport) {
        viewport->stopPlayingScene();
        viewport->startPlayingScene();
    }
}

void PlaybackService::stopScene()
{
    playing = false;
    if (viewport) viewport->stopPlayingScene();
}

void PlaybackService::startSimulation()
{
    if (viewport) viewport->startPhysicsSimulation();
}

void PlaybackService::restartSimulation()
{
    if (viewport) viewport->restartPhysicsSimulation();
}

void PlaybackService::stopSimulation()
{
    if (viewport) viewport->stopPhysicsSimulation();
}
