/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PRESETEDIT_H
#define PRESETEDIT_H

// A PRESET IN A PROJECT IS THE PROJECT'S TO EDIT — the copy-on-write that
// makes it so (PRESET-EDIT-1; the owner, joint, 2026-09-21: "only the MASTER
// materials should be locked; if they are added to a project they should be
// editable already").
//
// WHAT WAS THERE BEFORE. A shipped preset is a read-only library bundle
// (MATERIAL_BUNDLE_SPEC phase 3): `MaterialBundle::write` refuses its
// reserved guid by name, the module's canvas took no edits, and the one way
// to change one was an explicit CUSTOMISE gesture that minted a SECOND
// material called "Wood PBR-1". So a user who added "Wood PBR" to their
// project and opened it found it locked — the material they had put in their
// own project was somebody else's.
//
// THE RULE NOW, in one line: the MASTER is locked, the project's copy is not,
// and the FIRST EDIT is what makes the copy. Nothing is minted when a preset
// is applied, listed or merely looked at; the moment an edit lands, the
// project's pin moves from the shared master to a bundle of its own —
//
//   * with the PRESET'S NAME. The user sees one material. This is the one
//     place a material may carry a shipped preset's name
//     (`MaterialBundle::createPresetCopy`), and `masterOf` below is the link
//     back to the preset it came from.
//   * with the preset's own MEMBERS (one object, shared — the bundle model).
//   * the library master UNTOUCHED, and with it every other project's pin:
//     that is the whole point of a copy-on-write, and it is what the old
//     "edit the library row" behaviour could not give.
//   * the scene's nodes RE-POINTED to the copy — the use edges move, and the
//     meshes are re-dressed from the copy's definition (one in-place material
//     swap each, `gi.material_swap`'s counters).
//
// AND IT IS ONE UNDO STEP (commands/presetcopycommand.h): the copy, the pin
// move and the use edges go back together, and the copy's row goes with them.
//
// WITH NO PROJECT OPEN THERE IS NOWHERE TO COPY TO, and that is the one
// refusal left: a master edited from the library view with no project is
// refused with the reason, which is `refusal()` below.

#include <QString>

class Database;
class Project;
class UndoService;
class SceneEditService;

namespace presetedit
{

/// THE SHIPPED PRESET BEHIND `guid`: the preset itself when `guid` names one
/// (by its reserved guid), the master recorded on a project's copy when it is
/// one, and empty for an ordinary material. The copy records it in its row's
/// `properties` — the same device the member stamp uses — because the link is
/// a fact about the row, not about the definition, and it must survive every
/// later definition write.
QString masterOf(Database *db, const QString &guid);

/// Every project copy of `masterGuid` in the library, newest last. The
/// catalog has no index for it, so this is a walk of the material rows —
/// meant for a test and for the "which of these is mine?" question, never for
/// a per-frame path.
QStringList copiesOf(Database *db, const QString &masterGuid);

/// THIS PROJECT'S OWN COPY of the shipped preset `masterGuid` — the one it
/// PINS — or an empty string. It is what "Wood PBR" means inside a project
/// that has edited it, so the apply and the hover preview resolve a preset
/// through here: dropping the shipped tile again must not put a SECOND "Wood
/// PBR" in the project beside the user's own.
QString projectCopyOf(Database *db, Project *project, const QString &masterGuid);

/// WHY AN EDIT OF `guid` CANNOT HAPPEN AT ALL, in the user's words, or an
/// empty string when it can. Today there is exactly one reason: a shipped
/// preset with no project open.
///
/// `project` NULL MEANS "no project is open", and every caller must pass it
/// that way: the live Project instance is mutated in place and KEEPS ITS GUID
/// after a close (the app has one Project object, not one per project), so a
/// guid is not the question — `ScriptHost::isProjectOpen` /
/// `ProjectService::isSceneOpen` is.
QString refusal(Database *db, Project *project, const QString &guid);

/// The answer `forEdit` gives: the guid every later edit must use.
struct Target
{
    QString guid;            ///< the material to edit — the copy, once there is one
    QString master;          ///< the shipped preset behind it, or empty
    bool    copied = false;  ///< THIS call performed the copy-on-write
    QString error;           ///< empty when `guid` is usable

    bool ok() const { return !guid.isEmpty(); }
};

/// THE FIRST EDIT. Answers the guid to edit, performing the copy-on-write
/// when `guid` is a shipped preset and a project is open.
///
/// IDEMPOTENT AND CHEAP on everything else: an ordinary material (including a
/// copy made by an earlier call) is answered unchanged, with `copied` false
/// and no database write at all — so every edit door may call it on every
/// edit, which is what keeps the rule in one place.
///
/// `undo` may be null (a headless slice, a session with no undo service): the
/// copy is then performed directly and is not undoable, exactly as the
/// gestures in such a session are not. `sceneEdit` may be null; with one, the
/// meshes wearing the master are re-dressed from the copy.
Target forEdit(Database *db, Project *project, const QString &guid,
               UndoService *undo, SceneEditService *sceneEdit);

} // namespace presetedit

#endif // PRESETEDIT_H
