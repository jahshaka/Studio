/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITORTOOLBAR_H
#define EDITORTOOLBAR_H

// EditorToolbar — THE EDITOR'S TOOLBAR (D10-SHELL-MODULES): undo/redo, the
// gizmo modes, the transform space, the camera modes and THE camera speed,
// then (after the `editor.vr` slot and the spacer) Export, Save and Toggle
// Widgets, then the `editor.end` slot. The two slots are where contributed
// actions land (the VR module's toggle, the Claude assistant's chat). The
// toolbar's controls are VIEWS of the viewport's state: a key, a verb and a
// button all go through the slots below so the checked states follow.

#include <QObject>
#include <QString>
#include <QVariantList>

#include <functional>

class ActionHost;
class IEditorViewport;
class QAction;
class QActionGroup;
class QMainWindow;
class QSlider;
class QSpinBox;
class QToolBar;
class QToolButton;
class QtAwesome;

class EditorToolbar : public QObject
{
    Q_OBJECT
public:
    explicit EditorToolbar(QObject *parent = nullptr);

    struct Deps {
        QMainWindow *viewPort = nullptr;   ///< the editor page's nested window
        IEditorViewport *viewport = nullptr;
        QtAwesome *icons = nullptr;
        ActionHost *actions = nullptr;     ///< the toolbar's slots are registered here
        /// The undo/redo buttons, made by the shell's QUndoGroup (ModuleHub::
        /// createUndoAction): enabled and titled by the active stack.
        std::function<QAction *(QObject *)> undoAction;
        std::function<QAction *(QObject *)> redoAction;
        std::function<void()> exportScene;
        std::function<void()> saveScene;
        std::function<void()> toggleDocks;
        /// The viewport toast (snap size, camera speed).
        std::function<void(const QString &, const QString &)> toast;
        /// The editor is the active space (the snap keys act only there).
        std::function<bool()> editorActive;
    };
    void build(const Deps &deps);

    QToolBar *bar() const { return toolBar; }
    /// The force save: always shown, enabled while there is a world to save.
    QAction *saveAction() const { return actionSaveScene; }

    /// THE EDITOR TOOLBAR'S CONTROLS, as state (editor.toolbar): one entry per
    /// action with its objectName (minus the `action` prefix, lower-cased),
    /// whether it is on screen and whether it can be used.
    QVariantList toolbarActions() const;

    // ---- the gizmo -------------------------------------------------------
    void translateGizmo();
    void rotateGizmo();
    void scaleGizmo();
    /// Space: translate -> rotate -> scale -> translate (Unreal's mode cycle).
    void cycleGizmoMode();
    void useLocalTransform();
    void useGlobalTransform();
    /// "local" | "global" (editor.gizmoSpace / editor.setGizmoSpace).
    QString gizmoTransformSpace() const;
    bool applyGizmoTransformSpace(const QString &space);
    /// [ / ]: steps the ACTIVE gizmo's snap size; the translate size is also
    /// the ground grid's spacing (EDITOR_SHORTCUTS_SPEC §4).
    void stepSnapSize(int direction);

    // ---- the camera ------------------------------------------------------
    void useFreeCamera();
    void useArcballCam();
    /// Re-reads CameraSpeed into the speed button and its popover.
    void syncCameraSpeedUi();

private:
    Deps mDeps;
    IEditorViewport *sceneView = nullptr;
    QToolBar *toolBar = nullptr;
    QAction *actionTranslate = nullptr;
    QAction *actionRotate = nullptr;
    QAction *actionScale = nullptr;
    /// The toolbar's transform-space pair, held so a scripted
    /// editor.setGizmoSpace leaves the buttons telling the truth, exactly as
    /// actionTranslate/Rotate/Scale do for the gizmo mode.
    QAction *actionGlobalSpace = nullptr;
    QAction *actionLocalSpace = nullptr;
    QAction *actionSaveScene = nullptr;
    QActionGroup *transformGroup = nullptr;
    QActionGroup *transformSpaceGroup = nullptr;
    QActionGroup *cameraGroup = nullptr;
    /// THE CAMERA-SPEED BUTTON and the two controls in its popover (owner
    /// R15). Owned by the toolbar and the popover menu; held to keep all three
    /// in sync with CameraSpeed, which the verb and the scroll wheel can both
    /// change behind their backs.
    QToolButton *cameraSpeedButton = nullptr;
    QSlider *cameraSpeedSlider = nullptr;
    QSpinBox *cameraSpeedSpin = nullptr;
};

#endif // EDITORTOOLBAR_H
