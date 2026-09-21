/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALPRESETASSETS_H
#define MATERIALPRESETASSETS_H

// THE SHIPPED PRESETS AS READ-ONLY LIBRARY BUNDLES
// (MATERIAL_BUNDLE_SPEC §2.3 shape S3, phase 3; owner decision §12 Q2:
// "defaults are read-only SAMPLES… to edit you CREATE a material or CLONE a
// default").
//
// A preset used to be the one material shape that was not an asset at all:
// shipped JSON under `app/content/materials/`, no library row, and EVERY
// apply minted a project Material row — the owner's library carried "Gold
// PBR" three times over, an undo took none of them back, and each apply also
// wrote a `matgen.material` file and an "Presets" folder nobody ever read.
//
// EVERY PRESET IS A GRAPH (PRESET-UNIFY-1, 2026-09-20). A shipped preset is
// one file that authors its VALUES and names the graph beside it in
// `app/content/materials/graphs/`; the definition seeded here carries both,
// so selecting a preset shows its graph and `customise` hands the user a
// material the node editor can open. There is no second preset family: the
// seventeen `.effect` graph TEMPLATES that used to sit beside these in the
// module's drawer ("Brick" above "Brick PBR") are deleted.
//
// It is an ordinary library bundle now, with the guid it always had
// (`Constants::Reserved::DefaultMaterials` — the same id the tray tile, the
// drag payload and `materials.presets()` have carried for years), its maps as
// MEMBER textures in the content-addressed store, and its definition as the
// row's own `source` file like every other material. Applying it is
// `assets.addToProject` + the one apply: a PIN and a document edit, nothing
// minted, and the pin is undoable because it is pushed as a command.
//
// SEEDED ON FIRST USE, not at boot. That is the same rule its maps already
// followed (`ShippedAssets::pinTexture`: "imported through the ONE import
// pipeline the first time any project needs it") and it is the one that costs
// nothing: twenty bundles with forty-odd PNGs between them would otherwise
// be hashed and ingested during the first launch, on the thread that draws,
// for presets a user may never touch. Listing does not seed — a drawer lists
// from `MaterialPresets::all()` and the reserved guids, so the tiles are
// there before any row is.
//
// WHAT A HALF-DONE SEED LEAVES. `ensureSeeded` creates the row and then
// publishes the definition; if the publish fails it deletes the row it made,
// so a failure leaves nothing — except the store objects the maps were
// imported into, which belong to their own Texture rows and are ordinary
// library assets (an import is not undone by a failure further along, which
// is the house rule for imports). A row that somehow survives with NO
// definition is not an orphan either: `isSeeded` says no, and the next call —
// the seeder at the next launch, or the next apply — re-derives it in place.
//
// READ-ONLY IN FACT, NOT BY CONVENTION. `MaterialBundle::write` refuses a
// reserved preset guid by name (see `MaterialBundle::shippedPresetName`), so
// no editor, verb or autosave can publish over one — the module's save
// reports the refusal through the scene-issue bar rather than failing
// silently. The way to change a preset is `customise`, which is R18.

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

struct MaterialPreset;
class Database;
class Project;

namespace MaterialPresetAssets
{

/// The reserved guid of the preset `presetOrGuid` names — by NAME ("Gold
/// PBR", what a tile's role and every script call carry) or by the guid
/// itself. Empty when it names no shipped preset.
QString guidFor(const QString &presetOrGuid);

/// True when `presetOrGuid` names a shipped preset (either spelling).
bool isPreset(const QString &presetOrGuid);

/// WHAT A WORKER CAN DO AHEAD OF THE ROW PASS. Seeding a preset is mostly
/// NOT database work: it is hashing four megabytes of PNG per map (the
/// content id is how the library answers "I already have this") and decoding
/// a one-megabyte icon down to a tile. Measured on this box, that was 19-96
/// ms and 19-35 ms per preset respectively, against ~1 ms for the rows and
/// the publish — so all of it is prepared on a worker thread and handed to
/// `ensureSeeded`, which then costs a millisecond a preset on the thread
/// that draws (services/materialpresetseeder.h drives it).
///
/// A caller with no `Prepared` (an apply that beats the seeder) simply pays
/// for the one preset it needs, exactly as it did before the seeder existed.
struct Prepared
{
    QHash<QString, QString>    mapOids;       ///< map file path -> sha256
    QHash<QString, QByteArray> thumbnails;    ///< preset name -> stored tile (PNG)

    /// EVERY MAP ONCE, IN TABLE ORDER, WITH THE PRESET IT CAME IN THROUGH —
    /// (map file path, preset NAME), the file's FIRST namer in
    /// `MaterialPresets::all()` order.
    ///
    /// `mapOids` cannot answer either question: a QHash has no order, and it
    /// remembers no owner. Both matter to the seeder, which imports these
    /// files ITSELF (ahead of the row pass, on the pipeline's worker) and so
    /// must write the member stamp itself — `definitionFor`'s stamp fires only
    /// on a row IT minted, and by then these rows exist (SEED-STAMP-1). The
    /// origin is the first preset in table order because that is the one
    /// `definitionFor` would have recorded: it seeds in that order and
    /// `memberstamp::stamp` never overwrites an origin.
    QVector<QPair<QString, QString>> mapOwners;
};

/// Hash the maps and build the tiles for `presetNames` (all of them when the
/// list is empty). NO DATABASE, no GUI: safe on a worker, which is where the
/// seeder runs it.
Prepared prepare(const QStringList &presetNames = QStringList());

/// THE DEFINITION a preset seeds as: the same values
/// `BuiltinMaterials::fromPreset` builds — one conversion, so the picture
/// cannot drift from the one the hover preview shows — with every map
/// imported into the library by CONTENT and named by GUID.
/// `db` is required; no project is needed or used: a preset bundle is a
/// LIBRARY asset, and pinning is the caller's separate gesture.
QJsonObject definitionFor(const MaterialPreset &preset, Database *db,
                          QString *errorOut = nullptr, const Prepared *prepared = nullptr);

/// Is this preset's bundle already in the library, complete? (a Material row
/// with a definition that reads). The seeder asks it of all twenty before
/// it spawns a thread, and `ensureSeeded` asks it of one before it does any
/// work — one test, so "already seeded" cannot mean two things.
bool isSeeded(const QString &presetOrGuid, Database *db);

/// Seed the preset `presetOrGuid` names as a read-only library bundle and
/// answer its reserved guid. IDEMPOTENT: a row that already exists is
/// answered untouched (a preset is immutable, so there is nothing to
/// refresh). Empty when it names no preset, or on failure with `errorOut`.
QString ensureSeeded(const QString &presetOrGuid, Database *db, QString *errorOut = nullptr,
                     const Prepared *prepared = nullptr);

/// Every shipped preset, seeded. The count of rows that exist afterwards.
int seedAll(Database *db, QString *errorOut = nullptr);

/// THE NAME A NEW MATERIAL TAKES: `wanted` if it is free, else the first
/// free `<wanted>-N` (R18: "Name can be presetname-1 -2 -3 if there are
/// others"). Free is judged against the LIBRARY's material names AND every
/// shipped preset's name, so a Customise of Gold PBR is "Gold PBR-1" on the
/// first press whether or not the preset's own row exists yet. THE RULE
/// LIVES HERE, once, for every caller — including one that supplies a name.
QString customiseName(Database *db, const QString &wanted);

/// R18 — mint an EDITABLE copy of a preset in the library's custom drawer,
/// named by `customiseName` unless `name` is given. The copy is an ordinary
/// material bundle: the same member textures (one object, shared — that is
/// what the bundle model is for) and a fresh guid nothing calls reserved, so
/// every edit gesture works on it. Pinned into `project` when one is open, so
/// it lands in the project's drawer and the editor's tray too. Empty on
/// failure with `errorOut`.
QString customise(const QString &presetOrGuid, const QString &name,
                  Database *db, Project *project, QString *errorOut = nullptr);

} // namespace MaterialPresetAssets

#endif // MATERIALPRESETASSETS_H
