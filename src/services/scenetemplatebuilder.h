/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENETEMPLATEBUILDER_H
#define SCENETEMPLATEBUILDER_H

// THE NEW-SCENE TEMPLATES, BUILT (WORLD-MODEL-1; services/scenetemplate.h says
// what each holds). Basic and World: the sun (a directional light), the Sky
// Light, shadows on, the Epic world mode and the REALISTIC real-time sky with
// the sun following the atmosphere, standing on ordinary cube floors — one
// "Floor" for Basic, a 5 x 5 "World Floor" group for World. Empty is NOTHING:
// a root node, the Epic world mode, no sky and no lights.

#include "irisgl/irisglfwd.h"
#include "services/scenetemplate.h"

class Database;
class Project;

namespace scenetemplate {

/// The template's FLOOR alone: Basic's one "Floor" cube, or World's "World
/// Floor" group of 25; null for Empty. The same nodes `build` stands a new
/// scene on — and what scene.addFloor adds to an open one (the shipped samples
/// are re-authored onto it, SAMPLES-1). With a real project the tile is pinned
/// and each floor's Object row + tile dependency is written, as `build` does.
iris::SceneNodePtr buildFloor(SceneTemplate kind, Database *db, Project *project);

/// The template's document. With a real project (a guid), the floors' Object
/// rows and their tile dependency are written to `db` as addPrimitive writes
/// them for any cube.
iris::ScenePtr build(SceneTemplate kind, Database *db, Project *project);

}   // namespace scenetemplate

#endif   // SCENETEMPLATEBUILDER_H
