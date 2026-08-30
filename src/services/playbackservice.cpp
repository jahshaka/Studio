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

void PlaybackService::enterEditMode()
{
    playing = false;
    mode = SceneMode::EditMode;
    emit editModeEntered();
}

void PlaybackService::enterPlayMode()
{
    playing = true;
    mode = SceneMode::PlayMode;
    emit playModeEntered();
}

void PlaybackService::playScene()
{
    playing = true;
    mode = SceneMode::PlayMode;
    if (viewport) viewport->startPlayingScene();
}

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
