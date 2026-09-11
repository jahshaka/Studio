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
// Pure document composition: one shared cube mesh, five shared PbrMaterials,
// ~250 nodes under a single group. Everything is isBuiltIn and unpickable —
// the room is a workspace, never part of any exported document, and it must
// never crowd avatar playback (no shadows, no GI, ONE planar reflector — the
// floor plate; pinWorkspaceGrade below pins the rest of the page's grade).

#include "irisglfwd.h"

namespace avatar {

enum class SpaceMode { Grid, Modern };

namespace space {

/// The room's interior, in METRES — 1 unit is 1 m, like everywhere else in the
/// document. Published because the Avatar page reports the ceiling its subject
/// has to fit under: "the head touches the ceiling" (owner, 2026-09-08) was a
/// SCALE defect, and a number nobody outside this file could read was part of
/// why it went unnoticed.
constexpr float kRoomFloorSize = 10.0f;   ///< 10x10 m of floor tiles
/// The INTERIOR height: the floor plate's top surface is y = 0, the walls span
/// 0..4, and the ceiling slab's UNDERSIDE sits exactly here (the slab itself is
/// centred 1 cm higher and is 2 cm thick). This is the number a subject's head
/// has to stay under, which is why it is the interior and not the slab centre.
constexpr float kRoomInteriorHeight = 4.0f;

/// Builds the Tron room and returns its group node (already parented to the
/// scene root). Null when the plane primitive is unavailable (headless tests).
iris::SceneNodePtr buildModernRoom(const iris::ScenePtr &scene);

/// Pins the avatar workspace's GRADE on its own document (smoke S10).
///
/// The page's scene is a WORKSPACE, not a project scene: it is never saved,
/// never gets a World Mode picked on it, and its look has to be the same on
/// every box no matter what the user's project is set to. This states that in
/// one place instead of relying on iris::Scene's defaults happening to agree —
/// no tonemapper and no bloom (so a surface's brightness is exactly what the
/// materials say), a PINNED exposure if anyone ever turns HDR on here, and GI
/// OFF at a modest Rayon tier, because HlmsPbs's VCT/PCC binding is
/// process-wide and a preview that enabled GI would steal it from the editor.
void pinWorkspaceGrade(const iris::ScenePtr &scene);

/// String round-trip for the verb + the persisted setting.
const char *modeName(SpaceMode mode);
bool parseMode(const QString &name, SpaceMode *out);

} // namespace space
} // namespace avatar

#endif // AVATARSPACE_H
