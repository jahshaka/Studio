/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SUNLINK_H
#define SUNLINK_H

// Sun STEERING, panel side (SUN_AND_LIGHT_DEFAULTS_SPEC Q1/Q1e).
//
// The sky panel's "The Sky's Sun Steers the Sun Light" checkbox, and nothing
// else. WHICH light is the sun is not decided here and is not decided anywhere
// else either: `iris::Scene::sunLight()` is the ONE resolver, and this file's
// own depth-first "the selected directional, else the first one in traversal
// order" rule — one of the three that could disagree with each other — is gone
// with it (it changed its answer silently when a light was re-parented).
//
// What is left is the one line of policy the checkbox needs: how the toggle is
// applied so it lands as a single undo step on the same command `world.sky`'s
// `drivesSun` parameter uses.

#include <QString>

#include "irisgl/irisglfwd.h"

struct StudioServices;

namespace sunlink
{

/// Turns the sky's steering on or off. While it is on, the realistic sky's Sun
/// Azimuth/Elevation dials drive the SUN's rotation, whichever light that is;
/// turning it off hands that light back the rotation it had before.
/// Pushed through the undo stack when `services` has one, applied directly
/// otherwise (headless, tests). Returns the guid of the light being steered
/// afterwards — empty when steering is off OR when the scene has no directional
/// light to steer, so a caller can put its checkbox back where it was.
QString setDriven(const iris::ScenePtr &scene, StudioServices *services, bool on);

}

#endif // SUNLINK_H
