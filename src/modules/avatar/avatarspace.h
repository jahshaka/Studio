/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef AVATARSPACE_H
#define AVATARSPACE_H

// The avatar page's 3D environment (SPECS/AVATAR_SPACE_SPEC.md).
//
// Two looks, switched by a dropdown/verb:
//   Grid    — the founding minimal look: empty space, the lighting rig, no floor.
//   Modern  — THE TRON ROOM (owner design, 2026-09-04): a 10x10 grid of large
//             black mirror-gloss floor tiles over dead-black seams; walls of
//             white tiles whose seams GLOW (the emissive base plane shows
//             through the gaps); an all-white, slightly emissive ceiling that
//             doubles as the fill light. The Grid's visual language — the
//             room reads as lit-from-within architecture.
//
// Pure document composition: one shared plane mesh, five shared PbrMaterials,
// ~270 nodes under a single group. Everything is isBuiltIn and unpickable —
// the room is a workspace, never part of any exported document, and it must
// never crowd avatar playback (no shadows, no GI, no planar reflections —
// gloss + ambient carry the sheen).

#include "irisglfwd.h"

namespace avatar {

enum class SpaceMode { Grid, Modern };

namespace space {

/// Builds the Tron room and returns its group node (already parented to the
/// scene root). Null when the plane primitive is unavailable (headless tests).
iris::SceneNodePtr buildModernRoom(const iris::ScenePtr &scene);

/// String round-trip for the verb + the persisted setting.
const char *modeName(SpaceMode mode);
bool parseMode(const QString &name, SpaceMode *out);

} // namespace space
} // namespace avatar

#endif // AVATARSPACE_H
