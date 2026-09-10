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

// Sun coupling, panel side (VISUAL_PARITY re-audit F5).
//
// The sky panel and the verb both carry the "drive a directional light"
// toggle, so the three lines of policy live here instead of in either of them:
// which light a toggle would pick, and how the toggle is applied so it lands as
// one undo step on the same command `world.sunLight` uses. (It was written for
// TWO sky panels; they are one implementation since debt L6, and this is still
// the right home for the policy — the verb is the other caller.)

#include <QString>

#include "irisgl/irisglfwd.h"

struct StudioServices;

namespace sunlink
{

/// The light a "drive the sky's sun" toggle would link right now: the SELECTED
/// node when it is a directional light, else the scene's first directional
/// light in creation order. Null when the scene has no directional light —
/// which is the one case the toggle has to refuse.
iris::LightNodePtr resolveTarget(const iris::ScenePtr &scene,
                                 const iris::SceneNodePtr &selected);

/// Turns the coupling on or off. `on` links resolveTarget()'s light; !on
/// unlinks and hands the light back the rotation it had before it was driven.
/// Pushed through the undo stack when `services` has one, applied directly
/// otherwise (headless, tests). Returns the guid that is linked afterwards —
/// empty when unlinked OR when the request was refused for want of a light,
/// so a caller can put its checkbox back where it was.
QString setDriven(const iris::ScenePtr &scene, StudioServices *services, bool on,
                  const iris::SceneNodePtr &selected);

}

#endif // SUNLINK_H
