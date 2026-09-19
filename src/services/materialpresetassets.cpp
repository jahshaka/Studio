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

} // namespace

Prepared prepare(const QStringList &presetNames)
{
    Prepared out;
    for (const MaterialPreset &preset : MaterialPresets::all()) {
        if (!presetNames.isEmpty() && !presetNames.contains(preset.name)) continue;
        out.thumbnails.insert(preset.name, thumbnailFor(preset));
        for (const auto &slot : mapFiles(preset)) {
            if (slot.second.isEmpty() || out.mapOids.contains(slot.second)) continue;
            const QString oid = AssetCas::hashFile(slot.second);
            if (!oid.isEmpty()) out.mapOids.insert(slot.second, oid);
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
    for (const auto &slot : mapFiles(preset)) {
        if (slot.second.isEmpty()) continue;
        if (!QFileInfo(slot.second).isFile())
            return fail(QStringLiteral("'%1' names a map this build does not ship (%2)")
                            .arg(preset.name, slot.second));
        // THE ONE CONTENT IMPORT (P-2). The map becomes an ordinary library
        // Texture identified by its BYTES, so the same picture used by two
        // presets — and by a user who imports it themselves — is one object
        // and one row.
        // The content id from the worker when there is one: it is the same
        // sha256 this call would compute, and computing it here is 19-96 ms
        // of the thread that draws (see `Prepared`).
        const QString knownOid = prepared ? prepared->mapOids.value(slot.second) : QString();
        const ShippedAssets::Pinned pinned =
            ShippedAssets::importTexture(slot.second, QString(), db, nullptr, knownOid);
        if (!pinned.error.isEmpty() || pinned.guid.isEmpty())
            return fail(QStringLiteral("'%1' could not import %2: %3")
                            .arg(preset.name, QFileInfo(slot.second).fileName(),
                                 pinned.error.isEmpty() ? QStringLiteral("no row") : pinned.error));
        values[slot.first] = pinned.guid;
    }
    definition[QStringLiteral("values")] = values;
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
    // THE SUFFIX RULE, in ONE place and for EVERY caller (R18: "Name can be
    // presetname-1 -2 -3 if there are others"; fix round F10: a name the
    // caller supplied is bumped too, or two rows end up called "Gold PBR").
    // The rule is "the name you asked for, or the first free `<name>-N`".
    //
    // Bumped against the LIBRARY's material names — a drawer is a view of the
    // library, and with a project open it does not even list every material,
    // so deciding against a drawer would hand out a name already taken —
    // PLUS every shipped preset's name, whether or not it has been seeded
    // yet. That last part is what makes the default case read the way R18
    // asks: "Gold PBR" is a preset's name, so it is taken, so a Customise of
    // Gold PBR is "Gold PBR-1" on the first press and "-2" on the next,
    // whether or not the preset's own row exists.
    QSet<QString> taken;
    if (db)
        for (const auto &row : db->fetchAssetsForAssetView())
            if (row.type == static_cast<int>(ModelTypes::Material)) taken.insert(row.name);
    for (const MaterialPreset &preset : MaterialPresets::all()) taken.insert(preset.name);

    const QString base = wanted.trimmed();
    if (base.isEmpty()) return base;
    QString chosen = base;
    for (int n = 1; taken.contains(chosen); ++n) chosen = QStringLiteral("%1-%2").arg(base).arg(n);
    return chosen;
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

    // AN ORDINARY BUNDLE, with a guid nothing calls reserved: that is what
    // makes the copy editable where the preset is not. Its member textures
    // are the preset's own rows — one object, "used by 2" — which is the
    // point of a bundle owning members by reference.
    const QString copy = MaterialBundle::create(db, chosen, definition,
                                                thumbnailFor(preset), &error);
    if (copy.isEmpty())
        return fail(error.isEmpty() ? QStringLiteral("the library refused the copy") : error);

    // Into the project too, when there is one: the owner's gesture is "give me
    // this preset to edit", and a material they cannot see in their project
    // would be half an answer. Direct, because THEY asked for it.
    if (project && !project->getProjectGuid().isEmpty())
        ProjectAssets::addToProject(copy, db, project, ProjectAssets::AddKind::Direct);
    return copy;
}

} // namespace MaterialPresetAssets
