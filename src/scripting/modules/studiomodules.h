/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_STUDIOMODULES_H
#define SCRIPTING_STUDIOMODULES_H

// The one place Studio's API modules are assembled onto a ScriptEngine.
// Phase 1: project, scene, node, editor, app. Phase 2 adds assets, materials,
// graph, world here — nowhere else. Later domains (particles, anim) land here
// too, one line each.

class ScriptEngine;

void registerStudioModules(ScriptEngine &engine);

#endif // SCRIPTING_STUDIOMODULES_H
