#ifndef UIMANAGER_H
#define UIMANAGER_H

class AnimationWidget;
class QUndoStack;

class UiManager
{
public:
    static QUndoStack* undoStack;
    static AnimationWidget* animationWidget;

    static AnimationWidget *getAnimationWidget();
    static void setAnimationWidget(AnimationWidget *value);
};

#endif // UIMANAGER_H
