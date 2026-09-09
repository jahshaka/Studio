/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETREFS_H
#define ASSETREFS_H

// assetrefs — THE table of every key in a node object that names an ASSET,
// and the key-aware walk over it (CLIPBOARD_SPEC §3.2).
//
// WHY A TABLE AND NOT A GREP. A node object (src/io/sceneformat.h) carries two
// kinds of guid: NODE guids (its own identity, a socket owner, a physics
// constraint endpoint, a camera's focus target) and ASSET guids (the mesh, the
// texture slots, an IES profile, a skeletal clip, an avatar definition). They
// look identical — both are bare UUIDs — and they must be treated as opposites:
// a paste MINTS fresh node guids and must NEVER touch an asset guid, because an
// asset guid is identity (ASSET_PIPELINE invariant I1). Deciding which is which
// by shape is impossible, so it is decided by KEY, in one table, here.
//
// Three consumers share it: the clipboard's closure walker (what has to travel
// with a copied selection), the clipboard resolver (rewriting the rare guid a
// cross-library import had to land under a different id), and the .jaf node
// export (which computed its dependencies from AssetHelper::getChildGuids —
// NODE guids read as asset guids, an identity that a paste or a duplicate has
// broken since regenerateGuids landed, so a pasted node exported with no
// dependencies at all).
//
// THE TEST IS THE POINT. tests/services/test_clipboard.cpp asserts this table
// against a node object the real SceneWriter emitted for every node type, so a
// new reference key added to the writer without a row here FAILS a suite
// instead of silently dropping an asset out of every closure.

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace assetrefs {

/// One asset reference, with the dotted key path it was found at ("mesh",
/// "material.values.baseColorMap", "animations[0].skeletalAnimation.guid").
/// The path is for REPORTS — the missing-asset list names what needs the guid.
struct Ref
{
    QString guid;
    QString key;
};

/// Top-level node-object keys whose value is an asset guid.
const QStringList &nodeAssetKeys();

/// Keys inside `material.values` whose value is an asset guid: every texture
/// slot iris::PbrMaterial declares plus `customPieceGraph` (the shader asset a
/// generated piece came from). Derived from PbrMaterial::mapRowNames(), never
/// re-spelled — a second spelling is how a picker and a walker drift apart.
const QStringList &materialAssetKeys();

/// Keys whose value is a NODE guid. Never an asset: these are what a PASTE
/// rewrites and what the closure walker must not report. Listed for the same
/// reason the asset keys are — so the two sets are one document.
const QStringList &nodeGuidKeys();

/// True for a guid this build reserves (`00000000-0000-0000-0000-…`): the
/// builtin primitives, shaders and material presets (src/data/constants.cpp).
/// They exist in every install, they are NOT catalog rows to be shipped, and
/// their ids deliberately COLLIDE across types (primitive 5 and shader 5 are
/// the same string) — so they never join a closure and are never remapped.
bool isReservedGuid(const QString &guid);

/// True when `value` is shaped like a guid this app mints (a bare UUID). A
/// texture slot may also hold a project-relative PATH when the texture is not
/// a catalog asset, and `mesh` may hold a `:`-prefixed builtin name; both
/// answer false and are left alone by every walk here.
bool isGuidValue(const QString &value);

/// Every asset guid `nodeObj` and its descendants reference, in walk order,
/// deduplicated. Reserved guids are excluded (see isReservedGuid).
QVector<Ref> collectAssetRefs(const QJsonObject &nodeObj);

/// The same, as a plain deduplicated guid list.
QStringList collectAssetGuids(const QJsonObject &nodeObj);

/// Rewrites the asset guids named by `map` throughout `nodeObj` (and its
/// descendants), by KEY. Returns how many values were rewritten. A guid the
/// map does not name is untouched — the map is only ever non-empty when a
/// cross-library resolve had to land an asset under a new id.
int remapAssetGuids(QJsonObject &nodeObj, const QHash<QString, QString> &map);

/// Rewrites the NODE guids named by `map` (the node's own `guid`, socket
/// owners, physics constraint endpoints, camera focus targets). The document
/// half of a paste does this on live nodes (iris::SceneNode::remapNodeReferences);
/// this is the JSON half, for envelopes that are rewritten before they are read.
int remapNodeGuids(QJsonObject &nodeObj, const QHash<QString, QString> &map);

} // namespace assetrefs

#endif // ASSETREFS_H
