/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "playbackservice.h"

void PlaybackService::enterEditMode()
{
    UiManager::isScenePlaying = false;
    UiManager::enterEditMode();
    emit editModeEntered();
}

void PlaybackService::enterPlayMode()
{
    UiManager::isScenePlaying = true;
    UiManager::enterPlayMode();
    emit playModeEntered();
}

bool PlaybackService::isPlaying() const
{
    return UiManager::isScenePlaying;
}

SceneMode PlaybackService::sceneMode() const
{
    return UiManager::sceneMode;
}

void PlaybackService::playScene()
{
    UiManager::playScene();
}

void PlaybackService::pauseScene()
{
    UiManager::pauseScene();
}

void PlaybackService::restartScene()
{
    UiManager::restartScene();
}

void PlaybackService::stopScene()
{
    UiManager::stopScene();
}

void PlaybackService::startSimulation()
{
    UiManager::startPhysicsSimulation();
}

void PlaybackService::restartSimulation()
{
    UiManager::restartPhysicsSimulation();
}

void PlaybackService::stopSimulation()
{
    UiManager::stopPhysicsSimulation();
}

bool PlaybackService::isSimulationRunning() const
{
    return UiManager::isSimulationRunning;
}

void PlaybackService::setSimulationRunning(bool running)
{
    UiManager::isSimulationRunning = running;
}
