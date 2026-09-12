/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETTRAY_H
#define ASSETTRAY_H

// THE EDITOR'S ASSET TRAY: every asset a scene uses, ONCE (owner rules,
// 2026-09-11 and 2026-09-12).
//
// 2026-09-11 (smoke S9): an asset is ONE thing in the editor. A single import
// mints several catalog rows — the Object, its Mesh member, a Texture member
// per image — making an avatar of it mints another (the Avatar row) and adding
// a Mixamo clip another still (the Animation row), and "Add to Project" pins
// the whole closure. The tray showed three tiles for one character.
//
// 2026-09-12 (lane L13): "the demo scenes — shouldn't all the assets in the
// scene be in the asset drawer?" The tray HID every asset something depended
// on (a global `NOT IN (SELECT dependee ...)` filter), and the editor records
// USE as a dependency — a material slot, the ground's tile, an image plane, a
// decal, a particle image, an applied material — so the moment a user applied
// a texture it vanished. On top of that the panel skipped the built-in
// primitives' rows, which in the samples were the only rows holding the
// textures' closure, and the panel and `assets.list({tray:true})` ran two
// different rules (the verb had neither the filter nor the skip).
//
// This is a VIEW rule, not a data change: nothing is deleted, no row moves,
// every guid still resolves, and the Assets PAGE (the library browser, where a
// user manages members) is untouched. It lives here, in ONE function —
// `list` — that the tray panel, its search and `assets.list({tray: true})`
// all call, so the three agree by construction (scripting.e2e.tray_panel
// compares the widget's tiles with the verb's rows).
//
// THE RULE. A listing is the folder's own rows plus, at the project root, the
// project's PINNED members. A row is hidden only when it is one of these —
//   1. an import MEMBER: its parent is another ASSET (the Mesh row, the member
//      Textures of a fresh import). (The `parent` column also holds a FOLDER
//      guid for a filed asset, which is not membership.)
//   2. a MESH row, whatever its parent — a mesh is the inside of a model and
//      never a tile of its own (older content files its Mesh rows under the
//      project, where rule 1 cannot see them);
//   3. a model or clip an AVATAR in this project is built from — the avatar is
//      that character's tile (L2);
//   4. a scene node's OWN row: the built-in marker the editor writes for the
//      primitives, the default Ground, image planes and decals, and the row it
//      mints per particle EMITTER (a ParticleSystem row with no stored
//      definition — the emitter's recipe lives in the scene, not in the
//      catalog). A node is not a library asset; what it USES is;
//   5. a texture added DIRECTLY whose companion material (minted for it at
//      add time, ImageMaterial's `companionOf` stamp) is in this project and
//      is the ONLY thing in this project that uses it — the material is that
//      image's tile (an asset is one thing). The moment a scene node uses the
//      image itself (a material slot, the ground, a decal, a particle, a
//      light) it is a tile of its own.
// USE EDGES NEVER HIDE ANYTHING. Every other row the project holds or pins is
// a tile, once.

#include <QString>
#include <QStringList>
#include <QVector>

#include "data/project.h"

class Database;

namespace assettray {

/// THE TRAY LISTING: what the editor's asset tray shows inside `folderGuid`
/// (the project's own guid = the root) of project `projectGuid` — the folder's
/// rows and, at the root, the project's pinned members, collapsed by the rule
/// above, order preserved, each asset once. `typeFilter` > 0 keeps one
/// ModelTypes value. Folders are not included (the panel lists them itself).
QVector<AssetRecord> list(Database *db, const QString &projectGuid, const QString &folderGuid,
                          int typeFilter = -1);

/// The rule applied to an arbitrary listing of `projectGuid`'s rows. The
/// overload taking `pinned` (the project's pinned members, which rule 3 needs)
/// is for a caller that has already read them — `list` has.
QVector<AssetRecord> collapse(Database *db, const QString &projectGuid,
                              const QVector<AssetRecord> &records);
QVector<AssetRecord> collapse(Database *db, const QString &projectGuid,
                              const QVector<AssetRecord> &records,
                              const QVector<AssetRecord> &pinned);

/// The guids `collapse` drops out of `records`.
QStringList hidden(Database *db, const QString &projectGuid,
                   const QVector<AssetRecord> &records);
QStringList hidden(Database *db, const QString &projectGuid,
                   const QVector<AssetRecord> &records,
                   const QVector<AssetRecord> &pinned);

}   // namespace assettray

#endif   // ASSETTRAY_H
