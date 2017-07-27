#include "uimanager.h"
#include "mainwindow.h"
#include "globals.h"
#include "core/project.h"
#include "widgets/sceneviewwidget.h"

#include <QUndoStack>
#include <QUndoCommand>

AnimationWidget *UiManager::animationWidget = nullptr;
SceneViewWidget *UiManager::sceneViewWidget = nullptr;

QUndoStack *UiManager::undoStack = nullptr;
SceneMode UiManager::sceneMode = SceneMode::EditMode;

MainWindow *UiManager::mainWindow = nullptr;

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
    // disable ui
}

void UiManager::enterEditMode()
{
    sceneViewWidget->stopPlayingScene();
    sceneMode = SceneMode::EditMode;
    // enable ui
}

void UiManager::updateWindowTitle()
{
    if (UiManager::isUndoStackDirty()) {
        UiManager::mainWindow->setWindowTitle("Jahshaka* - " + Globals::project->getFileName());
    } else {
        UiManager::mainWindow->setWindowTitle("Jahshaka - " + Globals::project->getFileName());
    }
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
    UiManager::mainWindow->setWindowTitle("Jahshaka* - " + Globals::project->getFileName());
}

// not really used
void UiManager::popUndoStack()
{
    UiManager::undoStack->undo();
}
