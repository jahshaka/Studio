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

// AN ASSET IS ONE THING IN THE EDITOR (owner rule, 2026-09-11).
//
// A single import mints SEVERAL catalog rows — the Object, its Mesh member, a
// Texture member per image — and making an avatar of it mints another (the
// Avatar row), and adding a Mixamo clip mints another still (the Animation
// row). Every one of those rows is pinned into the project by ONE "Add to
// Project" (the pin walks the dependency closure), so the editor's asset tray,
// which listed every pinned row it could see, showed THREE tiles for one
// character. The owner's words: an avatar added to the editor is a SINGLE
// asset — the mesh, the skeleton, the clips and the rig are reachable only
// inside the Avatar module.
//
// This is a VIEW rule, not a data change: nothing is deleted, no row moves,
// every guid still resolves, and the Assets PAGE (the library browser, where a
// user manages the members) is untouched. It is here, in one function, because
// the editor tray and `assets.list({tray: true})` must agree by construction —
// the verb is what the tests drive and the panel calls the same code.
//
// THE RULE, in order:
//   1. a row whose PARENT is another ASSET is a member of an import (the Mesh
//      row, the member Textures) — never a tray citizen. (The same column also
//      holds a FOLDER guid for a filed asset, which is not membership.)
//   2. an Object that an Avatar row in the same listing is built from is shown
//      as that AVATAR (the avatar carries the model's thumbnail and its name);
//   3. an Animation row that an Avatar row in the same listing references is
//      the avatar's clip — it belongs inside the module, not in the tray. A
//      clip no avatar in the listing claims keeps its tile: it has no other
//      home.
//
// "In the same listing" is deliberate: hiding an Object because SOME avatar
// somewhere depends on it would empty the tray of a model whose avatar the
// project never added.
#include <QStringList>
#include <QVector>

#include "data/project.h"

class Database;

namespace assettray {

/// The listing collapsed to one row per asset closure, order preserved.
/// `claimants` are the AVATAR rows whose closures may swallow a row; when it
/// is empty the avatars found in `records` themselves are used (the tray's
/// case, where the listing is the whole truth).
QVector<AssetRecord> collapse(Database *db, const QVector<AssetRecord> &records,
                              const QVector<AssetRecord> &claimants = {});

/// The guids `collapse` would drop out of `records` (the same rule, for a
/// caller that already has widgets and only needs to know which to skip).
QStringList hidden(Database *db, const QVector<AssetRecord> &records,
                   const QVector<AssetRecord> &claimants = {});

}   // namespace assettray

#endif   // ASSETTRAY_H
