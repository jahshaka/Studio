#ifndef UIMANAGER_H
#define UIMANAGER_H

class AnimationWidget;
class QUndoStack;
class MainWindow;
class SceneViewWidget;

enum class SceneMode
{
    EditMode,
    PlayMode
};

class UiManager
{
public:
    static QUndoStack* undoStack;
    static AnimationWidget* animationWidget;
    static SceneViewWidget* sceneViewWidget;

    static AnimationWidget *getAnimationWidget();
    static void setAnimationWidget(AnimationWidget *value);

    static SceneViewWidget *getSceneViewWidget();
    static void setSceneViewWidget(SceneViewWidget *value);

    static void enterPlayMode();
    static void enterEditMode();

    static SceneMode sceneMode;

};

#endif // UIMANAGER_H
