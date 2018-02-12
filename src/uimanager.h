#ifndef UIMANAGER_H
#define UIMANAGER_H

class AnimationWidget;
class QUndoStack;
class QUndoCommand;
class MainWindow;
class SceneViewWidget;
class SceneHierarchyWidget;
class SceneNodePropertiesWidget;

enum class SceneMode
{
    EditMode,
    PlayMode
};

class UiManager
{
public:
    static MainWindow* mainWindow;
    static AnimationWidget* animationWidget;
    static SceneViewWidget* sceneViewWidget;
    static SceneHierarchyWidget* sceneHierarchyWidget;
	static SceneNodePropertiesWidget* propertyWidget;

    static AnimationWidget *getAnimationWidget();
    static void setAnimationWidget(AnimationWidget *value);

    static SceneViewWidget *getSceneViewWidget();
    static void setSceneViewWidget(SceneViewWidget *value);

    static void enterPlayMode();
    static void enterEditMode();
    static bool isSceneOpen;
    static bool isScenePlaying;
    static bool playMode;

    // playing functions
    static void playScene();
    static void pauseScene();
    static void restartScene();
    static void stopScene();

    static void updateWindowTitle();

    static bool isUndoStackDirty();
	static bool getUndoStackCount();
    static void clearUndoStack();
    static void setUndoStack(QUndoStack*);
    static void pushUndoStack(QUndoCommand*);
    static void popUndoStack();

    static SceneMode sceneMode;

private:
    static QUndoStack* undoStack;
};

#endif // UIMANAGER_H
