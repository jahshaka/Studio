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
#include "irisgl/document/materials/pbrmaterial.h"
#include "io/builtinmaterials.h"
#include "io/materialpresets.h"
#include "io/scenewriter.h"
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

QJsonObject definitionFor(const MaterialPreset &preset, Database *db, QString *errorOut)
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
        const ShippedAssets::Pinned pinned =
            ShippedAssets::importTexture(slot.second, QString(), db, nullptr);
        if (!pinned.error.isEmpty() || pinned.guid.isEmpty())
            return fail(QStringLiteral("'%1' could not import %2: %3")
                            .arg(preset.name, QFileInfo(slot.second).fileName(),
                                 pinned.error.isEmpty() ? QStringLiteral("no row") : pinned.error));
        values[slot.first] = pinned.guid;
    }
    definition[QStringLiteral("values")] = values;
    return definition;
}

QString ensureSeeded(const QString &presetOrGuid, Database *db, QString *errorOut)
{
    const auto fail = [errorOut](const QString &why) {
        if (errorOut) *errorOut = why;
        return QString();
    };
    if (!db) return fail(QStringLiteral("no library"));

    const QString guid = guidFor(presetOrGuid);
    if (guid.isEmpty())
        return fail(QStringLiteral("'%1' names no shipped preset").arg(presetOrGuid));

    // IDEMPOTENT, and that is the whole seeding policy: a preset is immutable,
    // so a row that exists is the answer. (A row whose definition failed to
    // publish is repaired by the write below, because `write` is what decides
    // whether the row has one.)
    const AssetRecord existing = db->fetchAsset(guid);
    if (!existing.guid.isEmpty()
        && existing.type == static_cast<int>(ModelTypes::Material)
        && !MaterialBundle::read(db, guid).isEmpty())
        return guid;

    bool found = false;
    const MaterialPreset preset = MaterialPresets::find(presetOrGuid, &found);
    if (!found) return fail(QStringLiteral("'%1' names no shipped preset").arg(presetOrGuid));

    QString error;
    const QJsonObject definition = definitionFor(preset, db, &error);
    if (definition.isEmpty()) return fail(error);

    if (existing.guid.isEmpty())
        db->createAssetEntry(guid, preset.name, static_cast<int>(ModelTypes::Material),
                             QString(),          // a library row: no parent folder
                             QString(),          // a library row: no project guid
                             QString(), QString(), thumbnailFor(preset),
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

QString customiseName(Database *db, const QString &presetName)
{
    // THE SUFFIX RULE, in ONE place (R18: "Name can be presetname-1 -2 -3 if
    // there are others"). Bumped against the LIBRARY's material names — a
    // drawer is a view of the library, and with a project open it does not
    // even list every material, so deciding against a drawer would hand out a
    // name that is already taken.
    QSet<QString> taken;
    if (db)
        for (const auto &row : db->fetchAssetsForAssetView())
            if (row.type == static_cast<int>(ModelTypes::Material)) taken.insert(row.name);

    int n = 1;
    QString chosen = QStringLiteral("%1-%2").arg(presetName).arg(n);
    while (taken.contains(chosen)) chosen = QStringLiteral("%1-%2").arg(presetName).arg(++n);
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

    const QString chosen = name.trimmed().isEmpty() ? customiseName(db, preset.name)
                                                    : name.trimmed();
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
