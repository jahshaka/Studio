/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VIEWCONTROLLER_H
#define VIEWCONTROLLER_H

// ViewController — THE EDITOR'S VIEW OF THE WORLD, AS CONTROLS (D10-SHELL-MODULES).
//
// The cameras menu, the canonical views and the projection toggle: the three
// controls of the viewport's bar that say what the editor camera looks
// through. They are views of the VIEWPORT's state — the viewport owns the
// camera, the per-view memory and the axis-view rotation lock — and every path
// that changes a view (the dropdown, the view.* chords, the projection button,
// editor.setView) lands in applyCameraView, so the button and the menu can
// never disagree about which view the editor is in.

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class IEditorViewport;
class QAction;
class QMenu;
class QPushButton;
class QToolButton;

class ViewController : public QObject
{
    Q_OBJECT
public:
    explicit ViewController(QObject *parent = nullptr);

    /// The viewport the controls follow (made after the controls are).
    void setViewport(IEditorViewport *viewport);

    /// The three buttons of the viewport bar, built once.
    struct CameraControls {
        QPushButton *projection = nullptr;   ///< the perspective/orthographic toggle
        QToolButton *views = nullptr;        ///< Views ▾ (the canonical views)
        QToolButton *cameras = nullptr;      ///< Camera ▾ (the explorer + scene cameras)
    };
    CameraControls createCameraControls();

    /// Views dropdown / view.* shortcuts / editor.setView verb — ONE path:
    /// snaps the editor camera to a canonical view ("top", "bottom", "left",
    /// "right", "front", "back", "perspective"), switches projection (axis
    /// views are orthographic) and keeps the toolbar + dropdown checks in
    /// sync. Returns false for an unknown name.
    bool applyCameraView(const QString &name);
    /// The PROJECTION TOGGLE, and it is a CANONICAL VIEW change (hygiene lane,
    /// 2026-09-09). `true` = perspective; `false` = the last orthographic axis
    /// view this window was in, "top" until there has been one.
    void changeProjection(bool perspective);
    /// The Views dropdown's label = the view it is currently in. Re-read from
    /// the viewport on every editor entry: a scene open resets it to
    /// perspective (per-view camera memory is per scene session).
    void followViewport();
    /// Rebuilds the camera switcher's list from the live document
    /// (CAMERAS_SPEC D4). Connected to the menu's aboutToShow.
    void rebuildCamerasMenu();

private:
    void setViewsButtonLabel(const QString &view);
    /// Icon + tooltip only: what the projection button LOOKS like. Split out of
    /// changeProjection so the viewport can report a projection it changed
    /// itself without that report turning into a command.
    void syncProjectionButton(bool perspective);

    IEditorViewport *mViewport = nullptr;
    QPointer<QPushButton> mProjection;
    QPointer<QToolButton> mViewsButton;
    QMenu *mViewsMenu = nullptr;
    QVector<QAction *> mViewsActions;   // checkable, ordered as built
    /// Where the projection toggle goes when it is asked for "orthographic":
    /// the last axis view this window was in, so Perspective -> Front ->
    /// Perspective -> (toggle) returns to Front rather than jumping to Top.
    QString mLastOrthographicView = QStringLiteral("top");
    /// The CAMERA SWITCHER (CAMERAS_SPEC D4), beside Views: "Viewport" (the
    /// free explorer) plus every scene camera by name. Rebuilt from the
    /// document each time it opens — cameras are added, renamed and deleted
    /// while the menu exists, and a stale list would pilot a dead node.
    QPointer<QToolButton> mCamerasButton;
    QMenu *mCamerasMenu = nullptr;
};

#endif // VIEWCONTROLLER_H
