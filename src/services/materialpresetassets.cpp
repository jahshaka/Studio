/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/materialpresetassets.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/materialpreset.h"
#include "data/project.h"
#include "services/assettags.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "io/builtinmaterials.h"
#include "io/materialpresets.h"
#include "io/scenewriter.h"
#include "services/assetcas.h"
#include "services/materialbundle.h"
#include "services/materialtile.h"
#include "services/memberstamp.h"
#include "services/projectassets.h"
#include "services/shippedassets.h"

namespace MaterialPresetAssets
{

namespace {

/// The preset's icon as the row's stored thumbnail. A preset cannot be
/// rendered before it exists and a shipped tile is the picture the drawer has
/// always shown for it, so the icon IS the thumbnail — the same answer
/// `ImageMaterial` gives for a picture's companion. A render can replace it
/// later (THUMBS-1's rebuild sweep); an empty byte array is a perfectly good
/// answer when the icon is missing.
QByteArray thumbnailFor(const MaterialPreset &preset)
{
    if (preset.icon.isEmpty()) return QByteArray();
    QImage image(preset.icon);
    if (image.isNull()) return QByteArray();
    if (image.width() > 128 || image.height() > 128)
        image = image.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

/// The preset's texture files, paired with the material property each fills.
/// The pairing is `BuiltinMaterials::fromPreset`'s own — `pbrNormalMap` fills
/// the slot called `normalMap` — and it is written once here so the two
/// cannot disagree.
QVector<QPair<QString, QString>> mapFiles(const MaterialPreset &preset)
{
    return {
        { QStringLiteral("baseColorMap"), preset.baseColorMap },
        { QStringLiteral("metallicMap"),  preset.metallicMap },
        { QStringLiteral("roughnessMap"), preset.roughnessMap },
        { QStringLiteral("normalMap"),    preset.pbrNormalMap },
        { QStringLiteral("emissiveMap"),  preset.emissiveMap },
    };
}

/// EVERY IMAGE FILE A PRESET NAMES, once, in both halves of it: the map slots
/// and the authored graph's texture nodes (PRESET-UNIFY-1). The seeder hashes
/// this list on its worker and `definitionFor` imports exactly it, so what the
/// worker prepared is what the row pass needs and nothing is hashed twice on
/// the thread that draws.
QStringList imageFiles(const MaterialPreset &preset)
{
    QStringList files;
    for (const auto &slot : mapFiles(preset)) {
        if (slot.second.isEmpty() || files.contains(slot.second)) continue;
        files.append(slot.second);
    }
    for (const QJsonValue &value : preset.graph.value(QStringLiteral("nodes")).toArray()) {
        const QJsonObject node = value.toObject();
        if (node.value(QStringLiteral("type")).toString() != QLatin1String("texture")) continue;
        const QString file = node.value(QStringLiteral("value")).toString();
        if (file.isEmpty() || files.contains(file)) continue;
        files.append(file);
    }
    return files;
}

} // namespace

Prepared prepare(const QStringList &presetNames)
{
    Prepared out;
    for (const MaterialPreset &preset : MaterialPresets::all()) {
        if (!presetNames.isEmpty() && !presetNames.contains(preset.name)) continue;
        out.thumbnails.insert(preset.name, thumbnailFor(preset));
        for (const QString &file : imageFiles(preset)) {
            if (out.mapOids.contains(file)) continue;
            const QString oid = AssetCas::hashFile(file);
            if (oid.isEmpty()) continue;
            out.mapOids.insert(file, oid);
            // …and WHOSE map it is, in the order the presets are seeded: the
            // seeder stamps by this (see `Prepared::mapOwners`). Recorded in
            // the same first-wins step as the hash, so the two lists can never
            // disagree about which files this run prepared.
            out.mapOwners.append({ file, preset.name });
        }
    }
    return out;
}

QString guidFor(const QString &presetOrGuid)
{
    if (presetOrGuid.isEmpty()) return QString();
    // A reserved guid names a preset directly; so does its NAME, which is what
    // a tile's role and every script call carry.
    if (Constants::Reserved::DefaultMaterials.contains(presetOrGuid)) {
        bool found = false;
        MaterialPresets::find(presetOrGuid, &found);
        return found ? presetOrGuid : QString();
    }
    bool found = false;
    const MaterialPreset preset = MaterialPresets::find(presetOrGuid, &found);
    if (!found) return QString();
    return Constants::Reserved::DefaultMaterials.key(preset.name);
}

bool isPreset(const QString &presetOrGuid)
{
    return !guidFor(presetOrGuid).isEmpty();
}

QStringList allGuids()
{
    QStringList out;
    for (const MaterialPreset &preset : MaterialPresets::all()) {
        const QString guid = guidFor(preset.name);
        if (!guid.isEmpty() && !out.contains(guid)) out.append(guid);
    }
    return out;
}

bool isSeeded(const QString &presetOrGuid, Database *db)
{
    if (!db) return false;
    const QString guid = guidFor(presetOrGuid);
    if (guid.isEmpty()) return false;
    const AssetRecord row = db->fetchAsset(guid);
    return !row.guid.isEmpty()
           && row.type == static_cast<int>(ModelTypes::Material)
           && !MaterialBundle::read(db, guid).isEmpty();
}

QJsonObject definitionFor(const MaterialPreset &preset, Database *db, QString *errorOut,
                          const Prepared *prepared)
{
    const auto fail = [errorOut](const QString &why) {
        if (errorOut) *errorOut = why;
        return QJsonObject();
    };
    if (!db) return fail(QStringLiteral("no library"));

    // ONE CONVERSION, AND IT IS THE RENDERER'S. The values come from the same
    // `BuiltinMaterials::fromPreset` the hover preview and every apply before
    // this lane used, serialized by the same writer that writes a material
    // into a scene — so a seeded bundle IS the preset, to the bit, rather than
    // a second transcription of the same JSON that could drift from it.
    //
    // The maps are cleared first and substituted by GUID afterwards: a
    // definition may never name a file path (F3, and `MaterialBundle::write`
    // refuses one), and `SceneWriter` can only turn a path back into a guid
    // through a project's pins — which a library seed has none of.
    MaterialPreset stripped = preset;
    stripped.baseColorMap.clear();
    stripped.metallicMap.clear();
    stripped.roughnessMap.clear();
    stripped.pbrNormalMap.clear();
    stripped.emissiveMap.clear();

    QJsonObject definition;
    const iris::MaterialPtr built = BuiltinMaterials::fromPreset(stripped);
    SceneWriter::writeSceneNodeMaterial(definition, built);
    definition[QStringLiteral("name")] = preset.name;

    QJsonObject values = definition.value(QStringLiteral("values")).toObject();
    // EVERY IMAGE THIS PRESET NAMES, IMPORTED ONCE, whichever half names it:
    // the map slots above and the graph's texture nodes are two readings of
    // ONE material, so they must land on one library row each or the bundle's
    // members and the graph the user opens would disagree about what picture
    // is on it.
    QHash<QString, QString> guidForFile;
    const auto importOnce = [&](const QString &file, QString *errorOut) -> QString {
        if (file.isEmpty()) return QString();
        if (guidForFile.contains(file)) return guidForFile.value(file);
        if (!QFileInfo(file).isFile()) {
            if (errorOut)
                *errorOut = QStringLiteral("'%1' names a map this build does not ship (%2)")
                                .arg(preset.name, file);
            return QString();
        }
        // THE ONE CONTENT IMPORT (P-2). The map becomes an ordinary library
        // Texture identified by its BYTES, so the same picture used by two
        // presets — and by a user who imports it themselves — is one object
        // and one row.
        // The content id from the worker when there is one: it is the same
        // sha256 this call would compute, and computing it here is 19-96 ms
        // of the thread that draws (see `Prepared`).
        const QString knownOid = prepared ? prepared->mapOids.value(file) : QString();
        const ShippedAssets::Pinned pinned =
            ShippedAssets::importTexture(file, QString(), db, nullptr, knownOid);
        if (!pinned.error.isEmpty() || pinned.guid.isEmpty()) {
            if (errorOut)
                *errorOut = QStringLiteral("'%1' could not import %2: %3")
                                .arg(preset.name, QFileInfo(file).fileName(),
                                     pinned.error.isEmpty() ? QStringLiteral("no row")
                                                            : pinned.error);
            return QString();
        }
        // A PRESET'S MAP IS NOT ONE OF THE USER'S TILES (V-2, owner review
        // R10.2). It arrived INSIDE a material, exactly as a picture picked
        // through the material picker does, so it carries the same stamp and
        // folds into the bundle while only materials use it — and becomes a
        // tile again the moment the user puts it on a plane or picks it into
        // a material of their own. Without this the first-run seed put
        // twenty presets' worth of maps in the tray as if the user had
        // imported them.
        //
        // ONLY A MINTED ROW IS STAMPED, which is the other half of V-2: if
        // these bytes were already in the library under a row the USER
        // imported, that row is theirs and a seed may not quietly hide it.
        //
        // WHICH IS WHY THE SEEDER STAMPS ITS OWN IMPORTS and this call is not
        // the only writer (SEED-STAMP-1): on the route a person takes,
        // `MaterialPresetSeeder` mints these rows through the pipeline first
        // — off the thread that draws — so by the time the row pass gets here
        // the picture is an existing row, `minted` is false, and a stamp
        // written only here would never fire at all. `memberstamp::stamp` is
        // idempotent and never moves an origin, so the two writers agree on
        // every row they both see.
        if (pinned.minted)
            memberstamp::stamp(db, pinned.guid, MaterialPresetAssets::guidFor(preset.name));
        guidForFile.insert(file, pinned.guid);
        return pinned.guid;
    };

    for (const auto &slot : mapFiles(preset)) {
        if (slot.second.isEmpty()) continue;
        QString error;
        const QString guid = importOnce(slot.second, &error);
        if (guid.isEmpty()) return fail(error);
        values[slot.first] = guid;
    }
    definition[QStringLiteral("values")] = values;

    // AND THE GRAPH, NAMED BY GUID (PRESET-UNIFY-1). The preset's authored
    // graph rides the definition exactly as a module material's does — that
    // is what makes selecting a preset show its graph, and what makes
    // Customise hand the user a material they can open and edit rather than
    // an empty canvas. A definition may never name a file path (F3), and
    // `MaterialBundle::write` scans the graph payload too, so each texture
    // node's image goes through the same one import its map slot did.
    QJsonObject graph = preset.graph;
    if (!graph.isEmpty()) {
        QJsonArray nodes = graph.value(QStringLiteral("nodes")).toArray();
        for (int i = 0; i < nodes.size(); ++i) {
            QJsonObject node = nodes.at(i).toObject();
            if (node.value(QStringLiteral("type")).toString() != QLatin1String("texture"))
                continue;
            QString error;
            const QString guid = importOnce(node.value(QStringLiteral("value")).toString(),
                                            &error);
            if (guid.isEmpty()) return fail(error);
            node[QStringLiteral("value")] = guid;
            nodes[i] = node;
        }
        graph[QStringLiteral("nodes")] = nodes;
        definition[QStringLiteral("shadergraph")] = graph;
    }
    return definition;
}

QString ensureSeeded(const QString &presetOrGuid, Database *db, QString *errorOut,
                     const Prepared *prepared)
{
    const auto fail = [errorOut](const QString &why) {
        if (errorOut) *errorOut = why;
        return QString();
    };
    if (!db) return fail(QStringLiteral("no library"));

    const QString guid = guidFor(presetOrGuid);
    if (guid.isEmpty())
        return fail(QStringLiteral("'%1' names no shipped preset").arg(presetOrGuid));

    bool found = false;
    const MaterialPreset preset = MaterialPresets::find(presetOrGuid, &found);
    if (!found) return fail(QStringLiteral("'%1' names no shipped preset").arg(presetOrGuid));

    const AssetRecord existing = db->fetchAsset(guid);

    // A PRESET'S NAME IS PART OF ITS IDENTITY, so a row that drifted from it
    // is REPAIRED here rather than left. Every drawer labels a preset from
    // the shipped list (`MaterialPresets::all`), so a renamed row meant one
    // guid with two names, for ever: "Fred" in the Assets page and "Gold PBR"
    // in the Presets drawer. `assettags::write` refuses the rename at source
    // now; this heals a library that already took one.
    if (!existing.guid.isEmpty() && existing.name != preset.name)
        assettags::rename(db, guid, preset.name);

    // IDEMPOTENT, and that is the whole seeding policy: a preset is immutable,
    // so a row that exists with a definition is the answer. (A row whose
    // definition failed to publish — or was torn by a power cut, which the
    // preset's link-staged publish deliberately allows — falls through and is
    // re-derived by the write below.)
    if (isSeeded(guid, db)) return guid;

    QString error;
    const QJsonObject definition = definitionFor(preset, db, &error, prepared);
    if (definition.isEmpty()) return fail(error);

    const QByteArray tile = prepared && prepared->thumbnails.contains(preset.name)
                                ? prepared->thumbnails.value(preset.name)
                                : thumbnailFor(preset);
    if (existing.guid.isEmpty())
        db->createAssetEntry(guid, preset.name, static_cast<int>(ModelTypes::Material),
                             QString(),          // a library row: no parent folder
                             QString(),          // a library row: no project guid
                             QString(), QString(), tile,
                             QByteArray(), QByteArray(), QByteArray(),
                             AssetViewFilter::AssetsView);

    // THE SEEDER'S DOOR. Every other writer is refused on a reserved guid —
    // that refusal is what "read-only in fact" means — so the one place that
    // is allowed to publish a preset's definition says so at the call.
    const MaterialBundle::WriteResult written =
        MaterialBundle::writeShipped(db, guid, definition);
    if (!written.ok) {
        if (existing.guid.isEmpty()) db->deleteAsset(guid);
        return fail(written.error);
    }
    return guid;
}

int seedAll(Database *db, QString *errorOut)
{
    if (!db) {
        if (errorOut) *errorOut = QStringLiteral("no library");
        return 0;
    }
    int seeded = 0;
    for (const MaterialPreset &preset : MaterialPresets::all()) {
        QString error;
        if (!ensureSeeded(preset.name, db, &error).isEmpty()) { ++seeded; continue; }
        if (errorOut && errorOut->isEmpty()) *errorOut = error;
    }
    return seeded;
}

QString customiseName(Database *db, const QString &wanted)
{
    // THE SUFFIX RULE lives in ONE place: MaterialBundle::uniqueName (every
    // door that mints a material — Customise, the New dialog, an image's
    // companion — takes its name there). Kept as the module's verb.
    return MaterialBundle::uniqueName(db, wanted);
}

QString customise(const QString &presetOrGuid, const QString &name,
                  Database *db, Project *project, QString *errorOut)
{
    const auto fail = [errorOut](const QString &why) {
        if (errorOut) *errorOut = why;
        return QString();
    };
    if (!db) return fail(QStringLiteral("no library"));

    bool found = false;
    const MaterialPreset preset = MaterialPresets::find(presetOrGuid, &found);
    if (!found) return fail(QStringLiteral("'%1' names no shipped preset").arg(presetOrGuid));

    QString error;
    QJsonObject definition = definitionFor(preset, db, &error);
    if (definition.isEmpty()) return fail(error);

    // ONE NAMER FOR BOTH DOORS (F10): a caller-supplied name is bumped by the
    // same rule as the default, so two Customise calls with {name: "Fred"}
    // give "Fred" and "Fred-1" rather than two rows called "Fred".
    const QString chosen = customiseName(db, name.trimmed().isEmpty() ? preset.name
                                                                      : name.trimmed());
    definition[QStringLiteral("name")] = chosen;
    // THE GRAPH CARRIES A NAME TOO, and it is the one the module's Material
    // Settings shows and the one `buildDefinition` writes back on every save
    // (PRESET-UNIFY-1). Left at the preset's, "Gold PBR-1" would be called
    // "Gold PBR" in the settings panel and would RENAME ITSELF back the first
    // time the user saved it.
    if (definition.contains(QStringLiteral("shadergraph"))) {
        QJsonObject graph = definition.value(QStringLiteral("shadergraph")).toObject();
        QJsonObject settings = graph.value(QStringLiteral("settings")).toObject();
        settings[QStringLiteral("name")] = chosen;
        graph[QStringLiteral("settings")] = settings;
        definition[QStringLiteral("shadergraph")] = graph;
    }

    // AN ORDINARY BUNDLE, with a guid nothing calls reserved: that is what
    // makes the copy editable where the preset is not. Its member textures
    // are the preset's own rows — one object, "used by 2" — which is the
    // point of a bundle owning members by reference.
    // WITH A PROJECT OPEN IT IS THAT PROJECT'S (ASSETS-SCOPE-1): the copy is
    // pinned into the project below and is never a library tile; with none it
    // is a library material.
    const QString copy = MaterialBundle::create(db, chosen, definition,
                                                assethome::current(project),
                                                thumbnailFor(preset), &error);
    if (copy.isEmpty())
        return fail(error.isEmpty() ? QStringLiteral("the library refused the copy") : error);

    // Into the project too, when there is one: the owner's gesture is "give me
    // this preset to edit", and a material they cannot see in their project
    // would be half an answer. Direct, because THEY asked for it.
    if (project && !project->getProjectGuid().isEmpty())
        ProjectAssets::addToProject(copy, db, project, ProjectAssets::AddKind::Direct);
    // AND ITS TILE IS A RENDER OF IT (owner review R9(a)), HERE rather than at
    // every door: the copy carries the shipped preset's ICON until something
    // draws it, and that is a picture of the PRESET — not a sphere of this
    // material at all for silver or glass. Three doors called Customise (the
    // verb, the module's tile menu, the tray panel's) and each carried its own
    // copy of the call, so a fourth would have had to remember.
    // (services/materialtile.h: one gesture, one render, and a refusal is
    // logged by name instead of discarded.)
    materialtile::mint(db, project, copy, "Customise");
    return copy;
}

} // namespace MaterialPresetAssets
