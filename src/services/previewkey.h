/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SERVICES_PREVIEWKEY_H
#define SERVICES_PREVIEWKEY_H

// THE PREVIEW KEY (PREVIEWS-1, spec call 2): a hash of WHAT A PREVIEW DEPICTS, so "is this tile
// still the picture of this row?" is a comparison of two strings and never a render.
//
// A tile is stored with the key it was drawn from (the `thumbnails` row: `hash`, 16 hex digits);
// the key computed now from the catalog is compared with it. Equal = ok, different = stale.
//
// THE INPUTS, and only these (a key that moves on anything else redraws for nothing; one that
// misses an input shows a stale picture as current):
//   - the row's own stored content: its `asset_files` role:oid pairs (the definition, its bake);
//   - the OPEN PROJECT's pin of the row (`project_assets.oid_pin`): a tile is drawn pin-first;
//   - its MEMBERS' content: dependee:oid of every `dependencies` edge's `asset_files` row (the
//     textures a material samples, the mesh an Object draws);
//   - the stamp of the row's own BLOB (`assets.hash`, Database::blobStamp): an IN-PLACE edit of the
//     blob (Database::updateAssetAsset: a node's material, a sky's settings, a library save's cache)
//     moves it, so a picture requested before the edit and stored after it reads stale, never ok;
//   - the PRESET's version, and previewenv's version for a preset the studio lights.
// A MESH row depicts its BAKE alone: its key is the bake's role:oid (the "bake" role,
// iris::MeshBake::casRole) with the preset's and previewenv's versions - nothing else.
// NOT the name: a rename changes no picture. NOT the guid — except for a row with no content of
// its own (no file, no pin): an Object whose definition is its blob, fixed at import, keys on its
// guid. The two oid lists are SORTED first: the catalog answers them in no promised order.
//
// THE PRESETS ARE DATA: one row a type that has a preview. A type with no row has nothing to draw
// (state `none`): Sky until its preset arrives (PREVIEWS-1 stage 4). A Mesh with no bake has
// nothing to draw either (PreviewService).

#include <QString>
#include <QStringList>

#include "data/project.h"   // ModelTypes

namespace previews {

struct Preset
{
    const char *name = nullptr;
    int version = 0;          ///< bump with any change to how this preset draws
    bool studioLit = false;   ///< lit by previewenv: its version is in the key
};

/// The preset a library type draws with; null when the type has no preview.
const Preset *presetFor(int assetType);

/// What the catalog says a row depicts (Database::fetchPreviewRows reads it in one statement).
struct KeySource
{
    QString guid;
    int type = 0;
    QStringList files;     ///< "role:oid" of the row's own asset_files rows
    QString pin;           ///< the open project's oid_pin of the row; empty when none
    QStringList members;   ///< "dependee:oid" of its members' asset_files rows
    QString blob;          ///< the stamp of the row's blob (`assets.hash`); empty when it has none
};

/// The key's inputs from a catalog row (Database::PreviewRow: the service and its rows read one mapping).
template <class Row>
KeySource sourceOf(const Row &row)
{
    KeySource source;
    source.guid = row.guid;
    source.type = row.type;
    source.files = row.files;
    source.pin = row.pin;
    source.members = row.members;
    source.blob = row.blobStamp;
    return source;
}

/// The "bake:<oid>" entries of a row's files (a Mesh's key and its "has a bake" test read one list).
QStringList bakeOf(const QStringList &files);

/// NOTHING TO DRAW (state `none`), from the catalog alone: platform furniture (the app's own
/// content, shown in no tile grid), a MODEL row with no content at all (no file, no pin, no blob -
/// a builtin primitive's Object row, a template's Floor), or a MESH row with no bake ROW (the Mesh
/// preset draws the bake and nothing else). A bake FILE gone from the store with its row present
/// is not decided here (no file is opened at a listing): the row reads missing or stale and its
/// render fails by name.
template <class Row>
bool nothingToDraw(const Row &row)
{
    if (row.platform) return true;
    const auto type = static_cast<ModelTypes>(row.type);
    if (type == ModelTypes::Mesh) return bakeOf(row.files).isEmpty();
    const bool model = type == ModelTypes::Object || type == ModelTypes::ParticleSystem;
    return model && row.files.isEmpty() && row.pin.isEmpty() && !row.hasBlob;
}

/// The key, 16 lowercase hex digits; empty when the type has no preset. Pure: no catalog, no clock.
QString key(const KeySource &source, int previewenvVersion);

}   // namespace previews

#endif   // SERVICES_PREVIEWKEY_H
