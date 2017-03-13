#include "uimanager.h"


AnimationWidget *UiManager::animationWidget = nullptr;

AnimationWidget *UiManager::getAnimationWidget()
{
    return animationWidget;
}

void UiManager::setAnimationWidget(AnimationWidget *value)
{
    animationWidget = value;
}
