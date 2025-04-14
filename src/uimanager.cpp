/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "uimanager.h"
#include "mainwindow.h"
#include "globals.h"
#include "core/project.h"
#include "widgets/sceneviewwidget.h"

#include <QUndoStack>
#include <QUndoCommand>
#include <QDebug>

MainWindow *UiManager::mainWindow = Q_NULLPTR;
AnimationWidget *UiManager::animationWidget = Q_NULLPTR;
SceneViewWidget *UiManager::sceneViewWidget = Q_NULLPTR;
SceneHierarchyWidget *UiManager::sceneHierarchyWidget = Q_NULLPTR;
SceneNodePropertiesWidget *UiManager::propertyWidget = Q_NULLPTR;

QUndoStack *UiManager::undoStack = Q_NULLPTR;
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

SceneViewWidget *UiManager::getSceneViewWidget()
{
    return sceneViewWidget;
}

void UiManager::setSceneViewWidget(SceneViewWidget *value)
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
    return !UiManager::undoStack->isClean();
}

bool UiManager::getUndoStackCount()
{
	return UiManager::undoStack->count();
}

void UiManager::clearUndoStack()
{
    UiManager::undoStack->clear();
}

void UiManager::setUndoStack(QUndoStack *undoStack)
{
    UiManager::undoStack = undoStack;
}

void UiManager::pushUndoStack(QUndoCommand *command)
{
    UiManager::undoStack->push(command);
//    UiManager::mainWindow->setWindowTitle("Jahshaka* - " + Globals::project->getFileName());
}

// not really ever used...
void UiManager::popUndoStack()
{
    UiManager::undoStack->undo();
}