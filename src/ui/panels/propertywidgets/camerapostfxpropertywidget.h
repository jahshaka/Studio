/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CAMERAPOSTFXPROPERTYWIDGET_H
#define CAMERAPOSTFXPROPERTYWIDGET_H

// The camera panel's "Exposure & Post" section (CAMERA_LENS_SPEC §4/§5, P3+P4).
//
// A selected SCENE CAMERA can grade its own shot: its own exposure (a mode plus
// stops) and its own tri-state overrides over the world's post chain — bloom in
// particular, which is the row the owner asked for by name ("it's better
// there"). What it CANNOT do, and this panel says so in its tooltips rather
// than by omission, is pick an SMAA preset or an MSAA count: both are shader
// recompiles, so a per-camera one would hitch on every cut and they stay
// world-level.
//
// GENERATED FROM THE SAME TWO TABLES the World > Post Process section is built
// from (src/services/worldmodes.h — rows() filtered by postFxRowIds(), and
// postFxParams() for the continuous ones), intersected with the keys the
// DOCUMENT will store (iris::cameraPostKeys). This file names no effect, no
// range and no label; a row added to the world tables and to the document table
// appears here with no edit at all, and a row present in only one of them
// cannot appear, which is what keeps the panel and camera.postFx from
// disagreeing about what a camera can override.
//
// TRI-STATE, in the controls themselves and not in a mode switch beside them:
//   * an on/off effect is a THREE-item combo — "Inherit (on)", "On", "Off" —
//     because "inherit" is a value the user picks, not the absence of one, and
//     a checkbox has no room to say it (and would silently pin the inherited
//     value the moment anyone clicked it);
//   * a continuous parameter is an "Override" checkbox plus its number, greyed
//     while it inherits and showing the world's value there;
//   * a row the renderer does not serve yet renders as a disabled label, the
//     same way the World section shows it.
//
// Writes go through the same document doors the verbs take (setPostOverride /
// clearPostOverride and the exposure fields), so this panel is a second
// CONSUMER of the API layer and never a parallel implementation.

#include <QWidget>

#include "ui/controls/accordionbladewidget.h"
#include "irisgl/irisglfwd.h"

class IEditorViewport;

class CameraPostFxPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    CameraPostFxPropertyWidget();
    void setSceneNode(QSharedPointer<iris::SceneNode> node);
    /// The live viewport, so an edit is visible immediately. Nullable.
    void setSceneView(IEditorViewport *sceneView);

private:
    void rebuild();
    void applied(bool rebuildPanel);
    /// The world value this camera inherits for a row, already formatted for a
    /// label ("on", "off", "1.50", "not available").
    QString inheritedText(const QString &key) const;

    QSharedPointer<iris::CameraNode> camera;
    IEditorViewport *sceneView = nullptr;
};

#endif // CAMERAPOSTFXPROPERTYWIDGET_H
