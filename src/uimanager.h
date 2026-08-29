/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef UIMANAGER_H
#define UIMANAGER_H

class AnimationWidget;
class QUndoStack;
class QUndoCommand;
class MainWindow;
class IEditorViewport;
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
    static IEditorViewport* sceneViewWidget;
    static SceneHierarchyWidget* sceneHierarchyWidget;
	static SceneNodePropertiesWidget* propertyWidget;

    static AnimationWidget *getAnimationWidget();
    static void setAnimationWidget(AnimationWidget *value);

    static IEditorViewport *getSceneViewWidget();
    static void setSceneViewWidget(IEditorViewport *value);

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
    /// The stack itself — the scripting engine needs beginMacro/endMacro for
    /// one-undo-step-per-script wrapping. May be null before setupUndoRedo.
    static QUndoStack* getUndoStack() { return undoStack; }
    /// True while a script run's undo macro is open. clearUndoStack() must not
    /// clear then (QUndoStack::clear() inside an open macro corrupts the macro
    /// accounting) — e.g. a scripted project.create passes through newProject,
    /// which clears the stack for the UI flow.
    static bool scriptMacroOpen;
    static void clearUndoStack();
    static void setUndoStack(QUndoStack*);
    static void pushUndoStack(QUndoCommand*);
    static void popUndoStack();

    static SceneMode sceneMode;

private:
    static QUndoStack* undoStack;
};

#endif // UIMANAGER_H
