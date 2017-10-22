#include "uimanager.h"
#include "mainwindow.h"
#include "globals.h"
#include "core/project.h"
#include "widgets/sceneviewwidget.h"

#include <QUndoStack>
#include <QUndoCommand>

MainWindow *UiManager::mainWindow = Q_NULLPTR;
AnimationWidget *UiManager::animationWidget = Q_NULLPTR;
SceneViewWidget *UiManager::sceneViewWidget = Q_NULLPTR;
SceneHierarchyWidget *UiManager::sceneHierarchyWidget = Q_NULLPTR;

QUndoStack *UiManager::undoStack = Q_NULLPTR;
SceneMode UiManager::sceneMode = SceneMode::EditMode;

bool UiManager::isScenePlaying = false;
bool UiManager::playMode = false;

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
    sceneViewWidget->startPlayingScene();
    sceneMode = SceneMode::PlayMode;
}

void UiManager::enterEditMode()
{
    sceneViewWidget->stopPlayingScene();
    sceneMode = SceneMode::EditMode;
}

void UiManager::playScene()
{
    sceneViewWidget->startPlayingScene();
    sceneMode = SceneMode::PlayMode;
}

void UiManager::pauseScene()
{
    sceneViewWidget->pausePlayingScene();
    sceneMode = SceneMode::PlayMode;
}

void UiManager::restartScene()
{
    sceneMode = SceneMode::PlayMode;
    sceneViewWidget->stopPlayingScene();
    sceneViewWidget->startPlayingScene();
}

void UiManager::updateWindowTitle()
{
//    if (UiManager::isUndoStackDirty()) {
//        UiManager::mainWindow->setWindowTitle("Jahshaka* - " + Globals::project->getFileName());
//    } else {
        UiManager::mainWindow->setWindowTitle("Jahshaka - " + Globals::project->getProjectName());
//    }
}

bool UiManager::isUndoStackDirty()
{
    return !UiManager::undoStack->isClean();
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
