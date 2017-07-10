#include "uimanager.h"


AnimationWidget *UiManager::animationWidget = nullptr;
QUndoStack *UiManager::undoStack = nullptr;

AnimationWidget *UiManager::getAnimationWidget()
{
    return animationWidget;
}

void UiManager::setAnimationWidget(AnimationWidget *value)
{
    animationWidget = value;
}
