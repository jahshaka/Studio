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
    /// Ground grid (EDITOR_SHORTCUTS_SPEC §3): per-scene like showLightWires,
    /// default ON — absent in older scenes reads back as true.
    bool showGrid = true;
};

#endif // EDITORDATA_H
