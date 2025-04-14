/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef UIMANAGER_H
#define UIMANAGER_H

class AnimationWidget;
class QUndoStack;
class QUndoCommand;
class MainWindow;
class SceneViewWidget;
class SceneHierarchyWidget;
class SceneNodePropertiesWidget;

/*
Tied directly to the WindowSpaces enum
The sceneview widget is shared between the Editor and Player tabs
This enum distinguishes between which of each tab is active
*/
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

	static void startPhysicsSimulation();
	static void restartPhysicsSimulation();
	static void stopPhysicsSimulation();
	static bool isSimulationRunning;

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
