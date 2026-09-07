/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITORDATA_H
#define EDITORDATA_H

#include "irisgl/irisglfwd.h"

class EditorData
{
public:
    iris::CameraNodePtr editorCamera;
    float distFromPivot = 0.0f;
    bool showLightWires = true;
    bool showDebugDrawFlags = false;
    /// Ground grid (EDITOR_SHORTCUTS_SPEC §3): per-scene like showLightWires.
    ///
    /// DEFAULT OFF, and this member is one of THREE places that has to say so
    /// (owner report 2026-09-07: a brand-new scene came up with the grid on
    /// while a loaded one did not). The three, which the ui.grid_default gate
    /// asserts agree:
    ///   1. HERE — what a brand-new scene's EditorData is born with;
    ///   2. SceneReader::readEditorData — `editorObj["showGrid"].toBool(false)`,
    ///      the value a scene file that predates the key reads back as;
    ///   3. EngineSceneViewport::mShowGrid — the viewport's own initial state,
    ///      which is what a session shows before any scene is opened.
    /// It flipped to OFF on 2026-09-06 (scenes ship a tiled floor, so the
    /// perspective grid is opt-in) everywhere EXCEPT here, and this member is
    /// the one a new scene reads.
    bool showGrid = false;
};

#endif // EDITORDATA_H
