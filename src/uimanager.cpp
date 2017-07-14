#include "uimanager.h"
#include "widgets/sceneviewwidget.h"


AnimationWidget *UiManager::animationWidget = nullptr;
SceneViewWidget *UiManager::sceneViewWidget = nullptr;

QUndoStack *UiManager::undoStack = nullptr;
SceneMode UiManager::sceneMode = SceneMode::EditMode;

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
