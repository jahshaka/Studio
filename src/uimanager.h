#ifndef UIMANAGER_H
#define UIMANAGER_H

class AnimationWidget;

class UiManager
{
public:
    static AnimationWidget* animationWidget;

    static AnimationWidget *getAnimationWidget();
    static void setAnimationWidget(AnimationWidget *value);
};

#endif // UIMANAGER_H
