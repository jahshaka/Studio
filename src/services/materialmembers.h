/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALMEMBERS_H
#define MATERIALMEMBERS_H

// A BUNDLE'S MEMBERS, AND WHAT THE USER MAY DO TO THEM
// (SPECS/MATERIAL_BUNDLE_SPEC.md §3.3 V-2, §4, §6 — phase 2).
//
// Phase 1 made a material ONE row whose definition names its textures by
// guid, and `MaterialBundle::reconcileEdges` derives the membership edges
// from that definition on every write. This file is the layer above it: the
// four questions a person asks about those members, each answered ONCE so the
// Members panel, the verbs and the tray cannot give three answers.
//
//   1. WHAT IS IN THIS BUNDLE?      `describe` — name, slot or graph node,
//      size, used-by, baked or picked, pinned, unused. The projection
//      `materials.members` returns and the panel draws.
//
//   2. WHICH TEXTURES GET A TILE OF THEIR OWN?   `hiddenAsMember` — the
//      owner's decision Q4, rule V-2. A texture the USER imported is always a
//      tile. A texture that arrived THROUGH a material's picker is stamped
//      (`stampMember`) and folds into the bundle while only materials use it;
//      the moment anything else uses it — a scene node, a decal, an emitter,
//      the user adding it on its own — it is a tile again, because it is then
//      a thing of the user's.
//
//      V-3 ("hide every texture only materials use") is NOT this rule and was
//      rejected for a measured reason: hiding by DEPENDENCY was tried and
//      removed in 2026-09-12 — the editor records USE as a dependency, so the
//      user's own image vanished from their library the moment anything used
//      it (services/assettray.h, "USE EDGES NEVER HIDE ANYTHING"). The stamp
//      is what separates "came in inside a bundle" from "is used by a bundle",
//      and only the first one hides.
//
//   3. WHAT IS NO LONGER USED?      `unused` / `cleanUnused` — §4 L-2, owner
//      Q6: on demand, listing first, never at save. A member nothing
//      references is not deleted behind the user's back, and BYTES never go
//      here at all: the library scope removes the ROW, the project scope
//      removes the PIN, and the objects wait for `assets.gc`, whose own law
//      is that a dry run is the default.
//
//   4. "CHANGE THIS PICTURE FOR THIS MATERIAL ONLY."   `makeUnique` — §4. A
//      second Texture row over the SAME bytes (content-addressed: the store
//      gains nothing), swapped into this one material's definition. The other
//      materials that shared it never notice, and the user now has a row they
//      can edit without touching theirs.
//
// THE STAMP lives in the row's `properties` JSON, beside the `type` marker
// the editor writes for its own rows and the `companionOf` stamp an image's
// material carries:
//
//     { "member": true, "memberOf": "<the material it was picked into>" }
//
// `memberOf` is an ORIGIN, not the membership relation — membership is the
// edges, derived from the definition, and one texture may be a member of five
// materials. The origin is what lets "clean this bundle's unused textures"
// find the rows that came in through it and are now referenced by nothing.

#include <QString>
#include <QStringList>
#include <QVector>

class Database;
class Project;

namespace materialmembers {

struct Member
{
    QString guid;
    QString name;
    /// The definition slot it fills ("baseColorMap"), empty for a texture
    /// that only a graph node holds.
    QString slot;
    /// The graph node that holds it, when one does ("texture:<nodeId>");
    /// empty otherwise. Slot or node — the panel's second column.
    QString node;
    /// "baked" (a map the graph bake produced) or "source" (a picture).
    QString role;
    qint64 bytes = -1;      ///< the stored object's size; -1 = unknown
    int usedBy = 0;         ///< every asset and node that names it, not one project's
    bool pinned = false;    ///< by the OPEN project
    bool hidden = false;    ///< folds into the bundle under V-2
    bool member = false;    ///< carries the member stamp (came in through a picker)
};

/// Everything `materialGuid`'s definition names, in definition order. Reads
/// the definition PIN-FIRST when `project` is given (a project sees its own
/// version), library-scope with none.
QVector<Member> describe(Database *db, Project *project, const QString &materialGuid);

/// Mark `textureGuid` as a picture that arrived INSIDE `materialGuid` (V-2).
/// Idempotent, and it never overwrites an existing origin: a texture first
/// picked into material A and later used by B belongs to A's clean-up, and
/// re-stamping it would move a row the user cannot see between owners.
bool stampMember(Database *db, const QString &textureGuid, const QString &materialGuid);

/// Does this row carry the stamp?
bool isStampedMember(Database *db, const QString &guid);

/// V-2, as a predicate over ONE row: stamped, used, and used by materials
/// only. False for every row the user imported themselves, for a stamped row
/// a scene node uses directly, and for a stamped row nothing uses at all
/// (that one is a tile so the user can see and remove it — it is also what
/// `unused` reports).
bool hiddenAsMember(Database *db, const QString &guid);

/// Every asset and scene node that names `guid` — the "used by N" count. The
/// UNSCOPED list (Database::hasMultipleDependers): a member's users are not
/// one project's business.
int usedBy(Database *db, const QString &guid);

struct Unused
{
    QString guid;
    QString name;
    qint64 bytes = -1;
    /// "library" — the ROW goes (a born-inside member with no user and no
    /// pin); "project" — the PIN goes (a member this project holds that
    /// nothing in it uses).
    QString scope;
};

/// WHAT A CLEAN WOULD REMOVE. With `materialGuid`, the rows that came in
/// through that bundle and are referenced by nothing (library scope); with
/// none, the member pins this project holds that nothing in it uses (project
/// scope — needs an open project). Never includes a row anything depends on,
/// and never a row another project pins.
QVector<Unused> unused(Database *db, Project *project, const QString &materialGuid = QString());

/// `unused`, then remove it for real — the row (library scope) or the pin
/// (project scope). BYTES ARE NEVER TOUCHED: superseded objects are
/// `assets.gc`'s business, which lists before it removes. Returns what it
/// removed (the same list `unused` answered, minus anything that failed).
QVector<Unused> cleanUnused(Database *db, Project *project, const QString &materialGuid,
                            QString *errorOut = nullptr);

/// A SECOND ROW OVER THE SAME BYTES, swapped into this material's definition
/// (§4, "change it for this material only"). The store gains nothing — the
/// object is content-addressed and already there — and every other material
/// that shared the texture keeps it. Returns the new texture's guid, empty on
/// failure with `errorOut` set.
QString makeUnique(Database *db, Project *project, const QString &materialGuid,
                   const QString &textureGuid, QString *errorOut = nullptr);

}   // namespace materialmembers

#endif   // MATERIALMEMBERS_H
