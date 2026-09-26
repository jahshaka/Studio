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

#include "data/constants.h"
#include "services/assethome.h"

class Database;
class Project;

namespace MaterialBundle
{

/// The definition format this build WRITES. v2 (and the unversioned image
/// material) still READ: a definition is JSON and older ones name their
/// members the same way.
inline constexpr int kDefinitionVersion = 3;

/// WHICH MATERIALS HAVE A DEFINITION FILE TODAY — say it plainly, because the
/// pin, the copy-on-write and the sidecar all follow from it and the answer is
/// not yet "all of them" (spec item F9, open):
///
///   * A MODULE material (a node graph, or one created by `materials.create`)
///     — YES. Every save goes through `write`, so it has a source file, a
///     pin, per-project copy-on-write and a sidecar.
///   * A material imported inside an archive — YES: the import re-publishes
///     its definition through `write` (ProjectArchiver).
///   * An IMAGE COMPANION (`ImageMaterial::createMaterialAsset`, minted when a
///     picture is added to a project) — NO, not yet. It is still a row BLOB
///     only, so it has no pin of its own: editing it in the library changes
///     every project using it, and `assets.rebuildCatalog` cannot restore it.
///     `read` falls back to the blob for exactly this kind, so everything
///     RESOLVES; what it lacks is the versioning. It is a one-member bundle
///     already (§9.3) and moving it is a small change — held only because the
///     lead is deciding where it lands.
///   * A PRESET — NO: a preset is shipped JSON with no library row at all
///     until phase 3 seeds it as a read-only bundle.

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
///
/// ATOMICITY (F19): the CATALOG half — the store's file rows, the source
/// pointer or the pin, the blob, the edges, the member pins — is one
/// transaction. It all lands or none of it does; nothing partial is ever
/// committed. The BYTES are written first, because they are content-addressed
/// and a failure then leaves an object no row names, which `assets.gc`
/// collects; the reverse (a row naming bytes that are not there) is the
/// failure that cannot be repaired, and this function cannot produce it.
WriteResult write(Database *db, Project *project, const QString &guid,
                  const QJsonObject &definition, Scope scope = Scope::Library);

/// THE NAME OF THE SHIPPED PRESET `guid` reserves, or an empty string
/// (`Constants::Reserved::DefaultMaterials`). A preset is READ-ONLY IN FACT,
/// not by convention: `write` refuses one and names it, so no editor, verb or
/// 1.5 s autosave can publish over a material the app ships. The owner's
/// model (§12 Q2): "defaults are read-only SAMPLES… to edit you CREATE a
/// material or CLONE a default" — `MaterialPresetAssets::customise` is the
/// clone, and it makes an ordinary bundle with an ordinary guid.
///
/// INLINE, because the read-only law has more than one enforcement point and
/// they must all read the same table: the definition writer below, the NAME
/// (services/assettags.h — a rename would leave one guid with two names for
/// ever, since every drawer labels a preset from the shipped list), and the
/// module's "this does not open" (effectspage).
inline QString shippedPresetName(const QString &guid)
{
    if (guid.isEmpty()) return QString();
    return Constants::Reserved::DefaultMaterials.value(guid);
}

/// THE SHIPPED PRESET BEHIND A PROJECT'S COPY, read off the row's own
/// `properties` — the `presetMaster` link the copy-on-write writes
/// (PresetCopyCommand, PRESET-EDIT-1) — or empty for every row that is not a
/// project's copy. A link to anything that is not a reserved preset guid is no
/// link. No query: the caller holds the row. `presetedit::masterOf` reads
/// through this, so there is one answer to "is this a copy, and of what".
QString presetMasterOf(const QByteArray &rowProperties);

/// The row-properties key that link lives under — ONE spelling for the writer
/// (PresetCopyCommand) and the reader (presetMasterOf).
inline constexpr QLatin1StringView kPresetMasterKey{"presetMaster"};

/// WHY `projectGuid` MAY NOT PIN `guid`, in the user's words, or empty when it
/// may. One reason: `guid` is ANOTHER project's copy of a shipped preset
/// (pinned by some project and not by this one). Pinned here it would become
/// this project's copy too (`presetedit::projectCopyOf` answers the first copy
/// a project pins) and two projects would edit one material; the answer is the
/// master, and this project makes its own copy on its first edit. An UNPINNED
/// copy is allowed: that is the copy-on-write's own pin, one line after the
/// mint. `ProjectAssets::addToProject` asks this.
QString foreignCopyRefusal(Database *db, const QString &guid, const QString &projectGuid);

/// THE RESERVED GUID whose preset is called `name` (case-insensitively), or an
/// empty string. The mirror of `shippedPresetName`, and it exists because a
/// preset is reached BY NAME from a script, a drag payload and the tray: a
/// user's material that takes a preset's name is unreachable by that name for
/// ever (PRESET-UNIFY-1 fix round). `assettags::write` refuses one; so does
/// `MaterialBundle::create`.
inline QString shippedPresetGuidForName(const QString &name)
{
    if (name.isEmpty()) return QString();
    for (auto it = Constants::Reserved::DefaultMaterials.constBegin();
         it != Constants::Reserved::DefaultMaterials.constEnd(); ++it)
        if (it.value().compare(name, Qt::CaseInsensitive) == 0) return it.key();
    return QString();
}

/// THE NAME A NEW MATERIAL MAY TAKE (BUNDLE-P4; the rule was
/// MaterialPresetAssets::customiseName, which forwards here now): `wanted`
/// itself, or the first free `<wanted>-N`, judged case-insensitively against
/// every material's name (`materialNames`) AND every shipped preset's reserved name —
/// whether or not the preset has been seeded (R18: "Gold PBR" is taken, so a
/// customise of it is "Gold PBR-1" on the first press). One rule for every
/// door that mints a material: Customise, the New dialog, an image's
/// companion. Empty in, empty out.
QString uniqueName(Database *db, const QString &wanted);

/// Every Material row's name — the library's and every project's own
/// (ASSETS-SCOPE-1) — the set a new material's name is judged against.
QStringList materialNames(Database *db);

/// THE SEEDER'S DOOR, and the only writer allowed on a reserved preset guid
/// (`MaterialPresetAssets::ensureSeeded` is its one caller). It publishes to
/// the LIBRARY exactly as `write` does — same guard, same colour
/// normalisation, same one transaction — so a preset bundle is stored,
/// membership-derived and sidecar-backed like every other material; the
/// separate name is the statement that seeding is a deliberate act rather
/// than an exception a caller can fall into.
WriteResult writeShipped(Database *db, const QString &guid, const QJsonObject &definition);

/// Mint a Material row carrying `definition` and write it through `write`.
/// `home` is WHERE THE ROW LIVES (services/assethome.h, ASSETS-SCOPE-1) and
/// every caller states it: the library (an explicit library gesture) or the
/// project whose editing made it (the project's own row, never a library
/// tile). `thumbnail` is the stored fallback (a render is asked for by
/// whoever made the gesture — THUMBS-1). Returns the new guid, empty on
/// failure.
QString create(Database *db, const QString &name, const QJsonObject &definition,
               const assethome::Home &home, const QByteArray &thumbnail = QByteArray(),
               QString *errorOut = nullptr);

/// THE PROJECT'S OWN COPY OF A SHIPPED PRESET (PRESET-EDIT-1, the owner's
/// rule: "only the MASTER materials should be locked; if they are added to a
/// project they should be editable already"). The same mint as `create` with
/// two differences, and both of them are the rule rather than a relaxation:
///
///   * IT KEEPS THE PRESET'S NAME. The user sees ONE material — they added
///     "Wood PBR" to their project and edited it — so the copy is called
///     "Wood PBR", not "Wood PBR-1". That is exactly the collision
///     `create` refuses (PRESET-UNIFY-1: a material carrying a preset's name
///     is unreachable BY NAME), and it is acceptable here because the copy
///     replaces the master in this project: `presetedit::masterOf` is the
///     link back, and the module's name lookup prefers the open project's
///     material over the shipped table.
///   * THE CALLER SUPPLIES THE GUID. An undone copy must be re-makeable
///     EXACTLY — same guid, same pins, same use edges — or a redo would
///     leave the scene wearing a material that no longer exists
///     (commands/presetcopycommand.h). Empty means "mint one".
///
/// Not a door for anything else: `create` is the one every other mint comes
/// through.
///
/// The copy is `projectGuid`'s OWN row (ASSETS-SCOPE-1) — never a library tile;
/// an empty `projectGuid` is refused.
QString createPresetCopy(Database *db, const QString &guid, const QString &projectGuid,
                         const QString &name, const QJsonObject &definition,
                         const QByteArray &thumbnail = QByteArray(),
                         QString *errorOut = nullptr);

/// Every asset this definition names: the texture guid in each texture slot,
/// plus the baked maps' member guids. Order preserved, each guid once. A slot
/// holding a path names nothing and is skipped (`write` refuses one anyway).
QStringList memberGuids(const QJsonObject &definition);

/// Reconcile `guid`'s dependency edges IN ONE SCOPE to exactly
/// `memberGuids(definition)` — the derivation that makes membership
/// undriftable. With no `projectGuid` that is the INTRINSIC (NULL-project)
/// set, the bundle's own membership; with one it is THAT PROJECT's set, which
/// is what a project-scope (copy-on-write) definition has — the library's
/// intrinsic edges must not move for it, and without a project set nothing
/// could see the members of a material a project owns. Edges whose DEPENDER
/// is somebody else (a node's USE of this material) are never touched.
/// Called by `write`; exposed for the catalog rebuild, which re-derives a
/// restored material's edges from its definition (audit G5).
bool reconcileEdges(Database *db, const QString &guid, const QJsonObject &definition,
                    const QString &projectGuid = QString());

/// The FIRST texture slot whose value is a FILE PATH rather than a guid, or an
/// empty string when the definition is clean. `slotOut` receives the slot's
/// name. Covers the definition's `values` AND a `shadergraph` payload's
/// texture nodes, because the graph is where the owner's absolute path got in.
QString offendingPath(const QJsonObject &definition, QString *slotOut = nullptr);

/// THE DOCUMENT'S SPELLING FOR A COLOUR. The graph evaluator's working shape
/// for a colour is `{r,g,b,a}` floats; every READER of a stored material —
/// `MaterialReader::parsePbrMaterial`, which reads an image material's
/// definition, a preset's and a node's copied values in the scene blob alike
/// — expects what `SceneWriter::writeSceneNodeMaterial` writes,
/// `QColor::name()`. Two spellings of one slot under one `materialType` is a
/// reader choosing by luck, and `QColor` of an object-valued key is invalid,
/// so the graph's red folded to BLACK. `write` runs this over every
/// definition, beside the path guard and for the same reason: so that no
/// caller can be the exception.
///
/// NOTE: `QColor::name()` is `#rrggbb` and DROPS ALPHA. That is the
/// document's existing convention (SceneWriter has always written it), and a
/// material's transparency lives in its own `alpha`/`alphaMode` rows, not in
/// a colour's fourth channel — but a graph that drives a colour socket's
/// alpha loses it at the store boundary, and that is a real limitation of the
/// convention rather than of this function.
QJsonObject normaliseColours(const QJsonObject &definition);

/// THE DOCUMENT'S SPELLING FOR THE UV TRANSFORM, and `write` runs it beside
/// `normaliseColours` for exactly the same reason. The graph evaluator folds
/// a transform into two-element ARRAYS (`textureScale: [4, 4]`,
/// `textureOffset: [u, v]`); the document has five scalar rows —
/// `textureScale`, `textureScaleV`, `textureOffsetU`, `textureOffsetV`,
/// `textureRotation` — and `MaterialReader::parsePbrMaterial` reads them as
/// FLOATS, where `QJsonValue::toDouble()` of an array is ZERO. So a material
/// saved with any non-identity tiling used to come back with its UV scale at
/// 0: one texel stretched over the whole surface, silently. A V value the
/// array does not carry follows U, which is the document's own default.
QJsonObject normaliseUv(const QJsonObject &definition);

/// True when `value` reads as a file path rather than an asset guid: it holds
/// a path separator, or a file extension. Asset guids carry neither.
bool looksLikePath(const QString &value);

} // namespace MaterialBundle

#endif // MATERIALBUNDLE_H
