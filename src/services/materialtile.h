/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALTILE_H
#define MATERIALTILE_H

// "GIVE THIS MATERIAL ITS OWN TILE" — once (PREVIEWENV-2 item c, owner review
// R9(a)).
//
// THE RULE, which is the owner's: a material's tile is a RENDER OF THAT
// MATERIAL on the studio sphere. A customised preset used to keep the SHIPPED
// PRESET'S ICON — a picture of the preset, and not a sphere at all for silver
// or glass — and a duplicate inherited the source row's tile, which is the
// right picture only until either material is touched.
//
// WHY A FUNCTION AND NOT FIVE COPIES. The call was written out at five doors
// (materials.createFromPreset, the module's Customise, the tray panel's
// Customise, materials.duplicate, the module's duplicate), each with the same
// six-line comment, each fetching the engine — and every one of them THREW THE
// OUTCOME AWAY. thumbrebuild::rebuildOne answers {ok, reason} precisely so a
// failed tile is never silent, and a discarded answer is a silent one: a
// refused borrow (the renderer busy with another render) left the fallback
// tile with nothing said anywhere.
//
// ONE GESTURE CAN AFFORD ONE RENDER. This is synchronous and it is called from
// a user's gesture; headless (or with the renderer busy) it fails by name and
// the fallback tile stands.

#include <QString>

class Database;
class Project;

namespace materialtile {

/// Renders `materialGuid`'s tile on the studio sphere and stores it. `who`
/// names the door in the log line when it cannot ("materials.duplicate", "the
/// module's Customise"). Answers whether a tile was stored; a caller that does
/// not care may ignore it — the reason has already been logged.
bool mint(Database *db, Project *project, const QString &materialGuid, const char *who);

}   // namespace materialtile

#endif   // MATERIALTILE_H
