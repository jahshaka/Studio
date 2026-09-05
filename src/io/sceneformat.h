/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEFORMAT_H
#define SCENEFORMAT_H

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

// -----------------------------------------------------------------------------
// THE SCENE FILE FORMAT — version 2 (SPECS/SCENEGRAPH_SPEC.md §3 step 4).
//
// v2 exists because v1 described a graph this program no longer has. Its node
// objects were a snapshot of `iris::SceneNode`'s FIELDS — a position, an euler
// triple, a child list that WAS the hierarchy — written by a walk over that
// list. There is exactly one hierarchy now and it is Ogre's (spec D2), a
// document node is a typed HANDLE onto it, and the file is a record of the
// handles' metadata plus the order the one tree holds them in.
//
// WHAT ACTUALLY CHANGED, and why each change is worth a version number:
//
//  1. `formatVersion`. v1 had no format version at all — `version` was the
//     app's CONTENT_VERSION, which moves for unrelated reasons and told a
//     reader nothing about the shape of what followed. A reader that cannot
//     name the format it is reading has to guess from key presence, which is
//     how "rot" ended up meaning two different things.
//
//  2. ROTATION IS A QUATERNION, once. v1 wrote the rotation TWICE: `rot` as an
//     euler triple (lossy — quaternion→euler→quaternion is not a fixed point in
//     float, so every save/reopen cycle rotated every rotated node a little
//     further) and `rotQuat` as the real thing, with the reader preferring the
//     second. v2 writes `rot` as the quaternion and nothing else. The two
//     spellings are told apart by the `scalar` key, which no euler triple has,
//     so the LEGACY node objects that still exist outside scene files (library
//     Object asset blobs — see below) keep reading correctly.
//
//  3. SCENE_STATIC is persisted, as an OVERRIDE (iris::StaticOverride). Not the
//     derived hint: `applyStaticDefaults` is a greedy policy and writing its
//     output would freeze today's policy into every document. Only the places a
//     human disagreed with it are written.
//
//  4. One writer per key. v1's type-specific writers reached back into the node
//     object and re-wrote keys the common writer had already written — `guid`
//     (mesh, particles) and `visible` (light, viewer, particles, decal) each had
//     up to four authors, and which one won depended on call order.
//
//  5. FRAGMENTS ARE FIRST CLASS (see SceneFragment). A subtree plus where it
//     belongs is now a named thing the writer produces and the reader consumes,
//     because three separate features want exactly that object: undo v1.5's
//     structural snapshots, the library's Object asset blobs (which have always
//     been a node object in a database row, just without a name), and script
//     copy/paste (`node.serialize` / `node.deserialize`).
//
// WHAT DID NOT CHANGE, deliberately. The node object stays FLAT — `material`,
// `children`, `name`, `type`, `mesh` at the top level — because the same object
// shape is a LIBRARY ASSET BLOB (`SceneWriter::writeSceneNode(blob, node,
// false)` in the import pipeline; `SceneReader::readSceneNode` on the other
// side in the asset viewer) and three more consumers walk those blobs by key
// (assetimporters' texture-guid substitution, AssetHelper::updateNodeMaterial,
// the Assets page's node tree). Regrouping the node object into `flags`/`data`
// sections would have rewritten a database format shipped inside every sample
// archive for a cosmetic gain. Recorded rather than done.
//
// NO BACKWARDS COMPATIBILITY as a product promise (owner decision D4): the app
// writes v2 and only v2, the six shipped samples are rebuilt in it, and nothing
// migrates user data. `SceneReader` still READS a v1 blob — that is what made
// rebuilding the samples possible, and it is what keeps a developer's existing
// projects openable — but it is a one-way conversion on load, announced in the
// log, and the next save writes v2.
// -----------------------------------------------------------------------------
namespace sceneformat
{

/// The `format` key's value. A blob that does not carry it is v1 (or a library
/// asset blob, which has never carried a header of any kind).
inline const char *kFormatId() { return "jahshaka.scene"; }

/// What SceneWriter emits today.
constexpr int kVersion = 2;

/// The version of `projectObj`, 1 for anything written before v2 existed.
inline int versionOf(const QJsonObject &projectObj)
{
    const int v = projectObj.value(QStringLiteral("formatVersion")).toInt(0);
    return v > 0 ? v : 1;
}

} // namespace sceneformat

/// A SUBTREE, plus where in the document it belongs.
///
/// This is the unit undo v1.5 captures, the unit `node.serialize` returns and
/// the unit the library stores as an Object asset. `node` is exactly what
/// SceneWriter::writeSceneNode produces (and SceneReader::readSceneNode
/// consumes); the rest is the anchor that makes a REBUILD land in the same
/// place the original stood — which is the whole content of audit F5.
struct SceneFragment
{
    /// The subtree, in the node-object shape documented above.
    QJsonObject node;
    /// The guid of the parent the subtree hung under. Empty means the scene's
    /// root node (whose own guid is a per-document value nothing else knows).
    QString parentGuid;
    /// Its DOCUMENT sibling index under that parent (iris::graph::indexInParent
    /// — engine-owned children are not counted). -1 = append.
    int siblingIndex = -1;

    /// The subtree's SESSION identities, in PRE-ORDER (the order both the
    /// writer's child walk and the reader's produce), preserved across a
    /// capture/rebuild pair so that undo of a delete gives back the same
    /// `nodeId`s the rest of the process is still holding — SceneMirror keys
    /// its engine entries on them and the scripting layer hands them out as
    /// `id`. Deliberately NOT part of the scene FILE (`nodeId` is minted per
    /// open and means nothing across runs); it lives here because a fragment is
    /// also an in-memory undo record.
    ///
    /// Pre-order and not a guid map on purpose: node guids are not reliably
    /// unique in content that shipped (the Particles sample carried two nodes
    /// on one guid until it was re-authored), and a map would then hand two
    /// nodes the same id — the exact aliasing SceneMirror's entry table cannot
    /// survive. Empty = mint fresh ids, which is what a PASTE wants.
    QVector<qint64> nodeIds;

    bool isNull() const { return node.isEmpty(); }
};

#endif // SCENEFORMAT_H
