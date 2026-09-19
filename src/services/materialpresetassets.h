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
// nothing: eighteen bundles with forty-odd PNGs between them would otherwise
// be hashed and ingested during the first launch, on the thread that draws,
// for presets a user may never touch. Listing does not seed — a drawer lists
// from `MaterialPresets::all()` and the reserved guids, so the tiles are
// there before any row is.
//
// READ-ONLY IN FACT, NOT BY CONVENTION. `MaterialBundle::write` refuses a
// reserved preset guid by name (see `MaterialBundle::shippedPresetName`), so
// no editor, verb or autosave can publish over one — the module's save
// reports the refusal through the scene-issue bar rather than failing
// silently. The way to change a preset is `customise`, which is R18.

#include <QJsonObject>
#include <QString>
#include <QStringList>

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

/// THE DEFINITION a preset seeds as: the same values
/// `BuiltinMaterials::fromPreset` builds — one conversion, so the picture
/// cannot drift from the one the hover preview shows — with every map
/// imported into the library by CONTENT and named by GUID.
/// `db` is required; no project is needed or used: a preset bundle is a
/// LIBRARY asset, and pinning is the caller's separate gesture.
QJsonObject definitionFor(const MaterialPreset &preset, Database *db, QString *errorOut = nullptr);

/// Seed the preset `presetOrGuid` names as a read-only library bundle and
/// answer its reserved guid. IDEMPOTENT: a row that already exists is
/// answered untouched (a preset is immutable, so there is nothing to
/// refresh). Empty when it names no preset, or on failure with `errorOut`.
QString ensureSeeded(const QString &presetOrGuid, Database *db, QString *errorOut = nullptr);

/// Every shipped preset, seeded. The count of rows that exist afterwards.
int seedAll(Database *db, QString *errorOut = nullptr);

/// R18 — the name a Customise copy takes: `<Preset>-1`, the suffix bumped
/// against the material names the library already holds (`Gold PBR-1`,
/// `Gold PBR-2`, …). THE RULE LIVES HERE, once, for every caller.
QString customiseName(Database *db, const QString &presetName);

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
