/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/uimanager.h"
#include "shell/mainwindow.h"
#include "shell/globals.h"
#include "data/project.h"
#include "viewport/ieditorviewport.h"
#include "services/undoservice.h"

#include <QUndoStack>
#include <QUndoCommand>
#include <QDebug>

MainWindow *UiManager::mainWindow = Q_NULLPTR;
AnimationWidget *UiManager::animationWidget = Q_NULLPTR;
IEditorViewport *UiManager::sceneViewWidget = Q_NULLPTR;
SceneHierarchyWidget *UiManager::sceneHierarchyWidget = Q_NULLPTR;
SceneNodePropertiesWidget *UiManager::propertyWidget = Q_NULLPTR;

UndoService *UiManager::undoService = Q_NULLPTR;
SceneMode UiManager::sceneMode = SceneMode::EditMode;

bool UiManager::isSceneOpen = false;
bool UiManager::isScenePlaying = false;
bool UiManager::playMode = false;

bool UiManager::isSimulationRunning = false;

void UiManager::startPhysicsSimulation()
{
    sceneViewWidget->startPhysicsSimulation();
}

void UiManager::restartPhysicsSimulation()
{
    sceneViewWidget->restartPhysicsSimulation();
}

void UiManager::stopPhysicsSimulation()
{
    sceneViewWidget->stopPhysicsSimulation();
}

IEditorViewport *UiManager::getSceneViewWidget()
{
    return sceneViewWidget;
}

void UiManager::setSceneViewWidget(IEditorViewport *value)
{
    sceneViewWidget = value;
}

AnimationWidget *UiManager::getAnimationWidget()
{
    return animationWidget;
}

void UiManager::setAnimationWidget(AnimationWidget *value)
{
    animationWidget = value;
}

void UiManager::enterPlayMode()
{
    //sceneViewWidget->startPlayingScene();
    sceneMode = SceneMode::PlayMode;
}

void UiManager::enterEditMode()
{
    //sceneViewWidget->stopPlayingScene();
    sceneMode = SceneMode::EditMode;

}

// TODO - check that the sceneMode being set here doesn't change anything anywhere else
// There should be no need to set the mode, see about removing (iKlsR)
void UiManager::playScene()
{
    isScenePlaying = true;
    sceneMode = SceneMode::PlayMode;
    sceneViewWidget->startPlayingScene();
}

void UiManager::pauseScene()
{
    isScenePlaying = false;
    sceneMode = SceneMode::PlayMode;
    sceneViewWidget->pausePlayingScene();
}

void UiManager::restartScene()
{
    isScenePlaying = true;
    sceneMode = SceneMode::PlayMode;
    sceneViewWidget->stopPlayingScene();
    sceneViewWidget->startPlayingScene();
}

void UiManager::stopScene()
{
    isScenePlaying = false;
    sceneViewWidget->stopPlayingScene();
}

void UiManager::updateWindowTitle()
{
//    if (UiManager::isUndoStackDirty()) {
//        UiManager::mainWindow->setWindowTitle("Jahshaka* - " + Globals::project->getFileName());
//    } else {
    UiManager::mainWindow->setWindowTitle(
        QString("%1 - %2").arg(mainWindow->originalTitle).arg(Globals::project->getProjectName())
    );
//    }
}

bool UiManager::isUndoStackDirty()
{
    return undoService->isDirty();
}

bool UiManager::getUndoStackCount()
{
	return undoService->count();
}

QUndoStack *UiManager::getUndoStack()
{
    return undoService ? undoService->stack() : Q_NULLPTR;
}

void UiManager::clearUndoStack()
{
    undoService->clear();
}

void UiManager::setUndoService(UndoService *service)
{
    UiManager::undoService = service;
}

void UiManager::pushUndoStack(QUndoCommand *command)
{
    undoService->push(command);
}

// not really ever used...
void UiManager::popUndoStack()
{
    undoService->stack()->undo();
}