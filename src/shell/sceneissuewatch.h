/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEISSUEWATCH_H
#define SCENEISSUEWATCH_H

// SceneIssueWatch — THE SCENE-ERROR AREA'S SHELL HALF (services/sceneissues.h,
// owner Q1b/Q1c; D10-SHELL-MODULES).
//
// A visible, dismissible list of the things wrong with the OPEN SCENE that the
// person using the editor can fix — beside the frame-rate readout, because that
// is where the owner asked for it. Engine diagnostics never come here: they go
// to the log and to the monitor's capture bundle. The bar is a view of
// SceneIssues and owns no state; this builds it lazily over the viewport and
// ticks the scanner. Nothing is wired INTO the bar: it has no buttons and emits
// nothing (owner, 2026-09-13 — it shows the errors and the user fixes them in
// the scene).

#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include <functional>

class IEditorViewport;
class QTimer;
class QWidget;
class SceneEditService;
class SceneIssueBar;

class SceneIssueWatch : public QObject
{
    Q_OBJECT
public:
    /// `window` parents the bar; `editorActive` answers whether the editor is
    /// the space on screen.
    SceneIssueWatch(QWidget *window, std::function<bool()> editorActive, QObject *parent = nullptr);

    void setScene(SceneEditService *sceneEdit, IEditorViewport *viewport);

    /// The scanner runs on a slow timer rather than per frame: the conditions
    /// it looks for are authoring state, not frame state, and raising an issue
    /// that is already live is a no-op by construction, so a second of latency
    /// costs nothing and a per-frame walk of every light against every mesh
    /// would. Also raises the library's own write failure (CLOSE-2 round 2, H7).
    void start();

    /// One pass of the scanner plus the show/hide decision for the bar. Driven
    /// by the 1 Hz timer and by every space switch; `editor.issueBar()` runs it
    /// before reporting, so a script reads a settled answer.
    void update();
    /// What the bar is showing, for that verb.
    QVariantMap state() const;

private:
    QWidget *mWindow = nullptr;
    std::function<bool()> mEditorActive;
    SceneEditService *mSceneEdit = nullptr;
    IEditorViewport *mViewport = nullptr;
    QPointer<SceneIssueBar> mBar;
    QTimer *mTimer = nullptr;
};

#endif // SCENEISSUEWATCH_H
