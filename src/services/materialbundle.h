/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALBUNDLE_H
#define MATERIALBUNDLE_H

// THE MATERIAL BUNDLE — one row, one definition, its textures as members
// (SPECS/MATERIAL_BUNDLE_SPEC.md §2, phase 1).
//
// A material is ONE library asset. Its DEFINITION is that row's CAS `source`
// file (decision D-2, the Avatar pattern): JSON naming its members by GUID and
// nothing else. That one move is what gives a material everything every other
// asset already had and it did not —
//
//   * a PIN. A project renders the definition it was built with, not whatever
//     the library row happens to hold now (F11: a library material's blob was
//     read live, so editing a library material silently changed every project
//     using it — the one asset kind that broke the reference-with-pin law).
//   * COPY-ON-WRITE. Editing a material from a project's drawer moves that
//     project's pin and leaves the library original alone (the owner's model,
//     §12 Q2).
//   * a SIDECAR. `assets.rebuildCatalog` can restore a material again (F12).
//   * ARCHIVES and GC for free: the definition is a store object like any
//     other, so nothing about it lives outside the store any more.
//
// THE THREE SHAPES COLLAPSED (§2.3). An image material, a node-graph material
// and a preset are one row with one definition; a graph is a PAYLOAD of that
// definition (`shadergraph`), never a second `ModelTypes::Shader` row. The
// definition's `values` are what a reader reads, so `MaterialReader::
// parseMaterialTyped` needs no branch for a graph material: the graph's
// evaluated output IS the material, and its baked maps are member textures
// named by GUID like every other map.
//
// MEMBERSHIP IS DERIVED, NEVER AUTHORED (M-B). The definition names the slots;
// `reconcileEdges` turns that into the dependency edges the closure walkers
// (add-to-project, remove, the archive manifest, the clipboard) already
// understand — on EVERY definition write, so the two cannot drift. Those
// intrinsic edges are written with a NULL project (audit G2): a bundle's own
// membership is a fact about the bundle, not about whichever project happened
// to be open, and `deleteProject` deletes edges by project stamp.
//
// AND NO PATH EVER REACHES A DEFINITION (F3, the third of the spec's three
// locks). `write` REFUSES a definition whose texture slot holds a file path
// instead of a guid — the defect that put
// "/home/…/AssetStore/<guid>/wood.jpg" in the owner's library, unopenable on
// any other machine. `offendingPath` is that guard as a predicate, so a
// hygiene suite can drive it over every definition a fixture produces.

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

class Database;
class Project;

namespace MaterialBundle
{

/// The definition format this build WRITES. v2 (and the unversioned image
/// material) still READ: a definition is JSON and older ones name their
/// members the same way.
inline constexpr int kDefinitionVersion = 3;

/// Every TEXTURE slot a material definition may carry, read once from
/// `iris::PbrMaterial`'s own property list. The slot list is the MATERIAL's —
/// a second transcription here is a place for the two to disagree, and the
/// detail-layer rows (twelve of them) are generated, so a transcription would
/// have been wrong the day a layer was added.
const QSet<QString> &textureSlots();

/// THE DEFINITION, however this row stores it. The CAS `source` file first —
/// PIN-first when `project` is given, so a project reads the version it was
/// built with — then the row's `asset` blob (an image companion minted before
/// this lane, a DB-only row). Empty when the guid names no material.
QJsonObject read(Database *db, const QString &guid, Project *project = nullptr);

/// WHOSE version of the definition a write publishes.
enum class Scope
{
    /// The LIBRARY's. The row's source pointer moves; projects keep their
    /// pins until they ask for the newer one.
    Library,
    /// THIS PROJECT's. The bytes land in the store and only this project's
    /// pin moves (`ProjectAssets::copyOnWrite`'s rule, by the same primitive):
    /// the library original and every other project are untouched.
    Project
};

struct WriteResult
{
    bool    ok = false;
    QString oid;      ///< the definition's content id
    QString error;    ///< empty when ok
};

/// THE ONE DEFINITION WRITE. Refuses a path in a texture slot; stores the
/// JSON as the row's `source` object; publishes it per `scope`; refreshes the
/// row's `asset` blob (a CACHE of the library's current definition, which is
/// what every listing and thumbnailer reads); re-derives the membership edges;
/// rewrites the sidecar. `project` may be null at Library scope.
WriteResult write(Database *db, Project *project, const QString &guid,
                  const QJsonObject &definition, Scope scope = Scope::Library);

/// Mint a LIBRARY Material row carrying `definition` and write it through
/// `write`. `thumbnail` is the stored fallback (a render is asked for by
/// whoever made the gesture — THUMBS-1). Returns the new guid, empty on
/// failure.
QString create(Database *db, const QString &name, const QJsonObject &definition,
               const QByteArray &thumbnail = QByteArray(), QString *errorOut = nullptr);

/// Every asset this definition names: the texture guid in each texture slot,
/// plus the baked maps' member guids. Order preserved, each guid once. A slot
/// holding a path names nothing and is skipped (`write` refuses one anyway).
QStringList memberGuids(const QJsonObject &definition);

/// Reconcile `guid`'s INTRINSIC (NULL-project) dependency edges to exactly
/// `memberGuids(definition)` — the derivation that makes membership
/// undriftable. Project-stamped edges (a node's USE of this material) are
/// never touched. Called by `write`; exposed for the catalog rebuild, which
/// re-derives a restored material's edges from its definition (audit G5).
bool reconcileEdges(Database *db, const QString &guid, const QJsonObject &definition);

/// The FIRST texture slot whose value is a FILE PATH rather than a guid, or an
/// empty string when the definition is clean. `slotOut` receives the slot's
/// name. Covers the definition's `values` AND a `shadergraph` payload's
/// texture nodes, because the graph is where the owner's absolute path got in.
QString offendingPath(const QJsonObject &definition, QString *slotOut = nullptr);

/// True when `value` reads as a file path rather than an asset guid: it holds
/// a path separator, or a file extension. Asset guids carry neither.
bool looksLikePath(const QString &value);

} // namespace MaterialBundle

#endif // MATERIALBUNDLE_H
