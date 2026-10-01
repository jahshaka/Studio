/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetmetadata.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QStandardPaths>
#include <QtEndian>
#include <algorithm>

#include "irisgl/import/meshbake.h"
#include "irisgl/import/modelsceneinfo.h"

#include "data/constants.h"
#include "services/animationfile.h"
#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/iesprofile.h"
#include "services/extentmeasure.h"
#include "services/rigsignature.h"
#include "irisgl/document/assets/avatardefinition.h"
#include "services/assetstorepaths.h"
#include <QSqlDatabase>
#include "data/project.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/core/logger.h"
#include "services/videoutils.h"

namespace {

qint64 sizeOf(const QString &filePath)
{
    return QFileInfo(filePath).size();
}

QString formatOf(const QString &filePath)
{
    return QFileInfo(filePath).suffix().toLower();
}

// Minimal RIFF/WAVE walk: fmt gives channels/rate/width, data gives length.
// Anything malformed just yields format + size (never throws, never guesses).
QJsonObject parseWavHeader(const QString &filePath, QJsonObject meta)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return meta;

    const QByteArray riff = file.read(12);
    if (riff.size() != 12 || !riff.startsWith("RIFF") || riff.mid(8, 4) != "WAVE")
        return meta;

    quint32 byteRate = 0;
    qint64 dataBytes = -1;
    while (!file.atEnd()) {
        const QByteArray header = file.read(8);
        if (header.size() != 8) break;
        const QByteArray id = header.left(4);
        const quint32 chunkSize = qFromLittleEndian<quint32>(header.constData() + 4);

        if (id == "fmt ") {
            const QByteArray fmt = file.read(qMin<quint32>(chunkSize, 16));
            if (fmt.size() < 16) break;
            const quint16 channels = qFromLittleEndian<quint16>(fmt.constData() + 2);
            const quint32 sampleRate = qFromLittleEndian<quint32>(fmt.constData() + 4);
            byteRate = qFromLittleEndian<quint32>(fmt.constData() + 8);
            const quint16 bits = qFromLittleEndian<quint16>(fmt.constData() + 14);
            meta["channels"] = channels;
            meta["sampleRate"] = static_cast<qint64>(sampleRate);
            meta["bitsPerSample"] = bits;
            if (chunkSize > 16) file.skip(chunkSize - 16 + (chunkSize & 1));
        } else if (id == "data") {
            dataBytes = chunkSize;
            break;   // fmt precedes data in every writer we care about
        } else {
            file.skip(chunkSize + (chunkSize & 1));
        }
    }

    if (byteRate > 0 && dataBytes >= 0)
        meta["duration"] = static_cast<qint64>(dataBytes * 1000.0 / byteRate);
    return meta;
}

} // namespace

QJsonObject AssetMetadata::forModelScene(const iris::ModelSceneInfo &scene, const QString &sourceFile)
{
    if (!scene.parsed) return QJsonObject();

    // Distinct texture references across every material and slot type;
    // embedded textures ("*0" paths) are references too, so a purely
    // embedded model still counts them.
    int textures = scene.textureReferences.size();
    if (textures == 0) textures = scene.embeddedTextures;

    // ---- THE RIG BLOCK (AVATAR_ASSET_SPEC §5.1) ---------------------------
    //
    // Whether a model is riggable is a property of the FILE, so it belongs in
    // the metadata every import already computes rather than in a second parse
    // the avatar module pays later. It is what `assets.list({rigged:true})`
    // filters on, what "Create Avatar" is offered from, and what the avatar
    // definition's `rig` block is copied from.
    //
    // BONE names come from the meshes' bone lists; NODE names from the scene
    // hierarchy (both read by IrisGL, in first-seen order). Both matter: the
    // clip <-> rig join is by SCENE-NODE name (rigsignature.h), and an FBX
    // carries bones the importer never gives a mesh.
    const QStringList &boneNames = scene.boneNames;
    const QStringList &nodeNames = scene.nodeNames;

    // Clips: name, LENGTH IN SECONDS (a file's ticks are meaningless to a
    // reader; the 25 fps fallback for a file that states none is applied at
    // the source, the same rule iris::Mesh::extractAnimations keys with), and
    // how much of the clip is bone work rather than importer pivot bookkeeping.
    QJsonArray animations;
    for (const auto &anim : scene.animations) {
        int boneChannels = 0;
        for (const QString &channel : anim.channelNames)
            if (!rig::isPivotChannel(channel)) ++boneChannels;
        QJsonObject clip;
        clip["name"] = anim.name;
        clip["length"] = anim.lengthSeconds;
        clip["channels"] = anim.channelNames.size();
        clip["boneChannels"] = boneChannels;
        animations.append(clip);
    }

    QJsonObject meta;
    meta["kind"] = "model";
    meta["format"] = formatOf(sourceFile);
    meta["fileSize"] = sizeOf(sourceFile);
    meta["vertices"] = scene.vertices;
    meta["triangles"] = scene.triangles;
    meta["meshes"] = scene.meshes;
    meta["materials"] = scene.materials;
    meta["textures"] = textures;
    meta["hasSkeleton"] = !boneNames.isEmpty();
    meta["bones"] = boneNames.size();
    meta["boneNames"] = QJsonArray::fromStringList(boneNames);
    meta["nodeNames"] = QJsonArray::fromStringList(nodeNames);
    meta["rigId"] = rig::rigId(boneNames);
    meta["animations"] = animations;

    // ---- SIZE, AS INFORMATION (services/extentmeasure.h) ------------------
    //
    // The model's measured size in metres and what the FILE declared its unit
    // to be. Computed HERE — the one import-time model-description site — so it
    // also comes for free on the lazy backfill (forModelBake, from the bake's describe). The extent is
    // IrisGL's measurement of the parse this import actually made, i.e. AFTER
    // the asset's import transform (SPECS/IMPORT_DIALOG_SPEC.md §6): it is the
    // size the user will see placed, because every instance is placed at
    // scale 1. No policy reads it; the import dialog shows it.
    extent::Extent measured;
    measured.x = scene.extentX;
    measured.y = scene.extentY;
    measured.z = scene.extentZ;
    measured.valid = scene.extentValid;
    extent::writeBlock(meta, measured, scene.declaredUnitScale);
    return meta;
}

namespace
{
AssetMetadata::BakePathResolver sBakePathResolver;
}   // namespace

void AssetMetadata::setBakePathResolver(BakePathResolver resolver)
{
    sBakePathResolver = std::move(resolver);
}

QString AssetMetadata::bakePathFor(int assetType, const QString &sourcePath,
                                   const QString &assetGuid)
{
    if (!sBakePathResolver || sourcePath.isEmpty()) return QString();
    return sBakePathResolver(assetType, sourcePath, assetGuid);
}

QJsonObject AssetMetadata::forModelBake(const QString &bakePath, const QString &sourceFile)
{
    // THE FACTS OF THE IMPORT'S OWN PARSE, kept in the bake (MeshBake::Model::
    // describe) — counted under the asset's import recipe, so the `extent` is
    // the size the asset MEASURES, exactly as the import's own block. No parse.
    if (bakePath.isEmpty()) return QJsonObject();
    const iris::MeshBake::Model model = iris::MeshBake::read(bakePath);
    if (!model.valid || !model.describe.parsed) return QJsonObject();
    return forModelScene(model.describe, sourceFile);
}

QJsonObject AssetMetadata::forClipBake(const QString &bakePath, const QString &sourceFile,
                                       const QString &baseName)
{
    if (bakePath.isEmpty()) return QJsonObject();
    const iris::MeshBake::Clip clip = iris::MeshBake::readClip(bakePath);
    if (!clip.valid) return QJsonObject();
    return forAnimationContents(animfile::describe(clip.info, baseName), sourceFile);
}

QJsonObject AssetMetadata::forImageFile(const QString &filePath)
{
    QJsonObject meta;
    meta["kind"] = "image";
    meta["format"] = formatOf(filePath);
    meta["fileSize"] = sizeOf(filePath);

    QImageReader reader(filePath);   // header-only: size() does not decode pixels
    const QSize size = reader.size();
    if (size.isValid()) {
        meta["width"] = size.width();
        meta["height"] = size.height();
    }
    return meta;
}

QJsonObject AssetMetadata::forAudioFile(const QString &filePath)
{
    QJsonObject meta;
    meta["kind"] = "audio";
    meta["format"] = formatOf(filePath);
    meta["fileSize"] = sizeOf(filePath);
    if (meta["format"].toString() == "wav")
        meta = parseWavHeader(filePath, meta);
    return meta;
}

QJsonObject AssetMetadata::forVideoFile(const QString &filePath)
{
    QJsonObject meta;
    meta["kind"] = "video";
    meta["format"] = formatOf(filePath);
    meta["fileSize"] = sizeOf(filePath);
    // The rich fields need the GUI thread (QMediaPlayer); on a worker this
    // stays a degraded block and ensure() will not persist it.
    if (VideoUtils::canUseMultimedia()) {
        const QJsonObject probed = VideoUtils::probeFile(filePath);
        for (auto it = probed.begin(); it != probed.end(); ++it)
            meta[it.key()] = it.value();
    }
    return meta;
}

QJsonObject AssetMetadata::forGenericFile(const QString &filePath)
{
    QJsonObject meta;
    meta["kind"] = "file";
    meta["format"] = formatOf(filePath);
    meta["fileSize"] = sizeOf(filePath);
    return meta;
}

QJsonObject AssetMetadata::forLightProfileFile(const QString &filePath)
{
    const IesProfile profile = IesProfile::parse(filePath);
    QJsonObject meta = profile.metadata(filePath);
    meta["kind"] = "lightprofile";
    // A row that fails to parse still gets a block (format/fileSize) so the
    // lazy backfill does not re-parse it on every inspection; the missing
    // photometric fields are the tell.
    return meta;
}

QJsonObject AssetMetadata::forAnimationContents(const animfile::Contents &contents,
                                                const QString &filePath)
{
    QJsonObject meta;
    meta["kind"] = "animation";
    meta["format"] = formatOf(filePath);
    meta["fileSize"] = sizeOf(filePath);

    if (!contents.parsed) {
        // A block is still written (format/fileSize) so the lazy backfill does
        // not re-describe an unreadable file on every inspection; the absent
        // clip table is the tell.
        meta["error"] = contents.error;
        return meta;
    }

    QJsonArray clips;
    for (const auto &clip : contents.clips) {
        QJsonObject entry;
        entry["name"] = clip.name;
        entry["rawName"] = clip.rawName;
        entry["length"] = clip.length;
        entry["channels"] = clip.channels;
        entry["boneChannels"] = clip.boneChannels;
        clips.append(entry);
    }
    meta["clips"] = clips;
    meta["duration"] = contents.duration;
    meta["bones"] = contents.boneChannelNames.size();
    meta["boneNames"] = QJsonArray::fromStringList(contents.boneChannelNames);
    // The join key with a model row's rig block: equal rigId = same skeleton.
    meta["rigId"] = contents.rigId;
    return meta;
}

QJsonObject AssetMetadata::forAvatarFile(const QString &filePath)
{
    QJsonObject meta;
    meta["kind"] = "avatar";
    meta["format"] = formatOf(filePath);
    meta["fileSize"] = sizeOf(filePath);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return meta;
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    iris::AvatarDefinition def;
    QString error;
    if (!iris::avatarDefinitionFromJson(obj, def, &error)) {
        // A row whose definition will not parse still gets a block, for the
        // same reason a bad .ies does: otherwise the lazy backfill re-parses
        // it on every inspection. The refusal is carried so the UI can SAY
        // what is wrong instead of showing a nameless tile.
        meta["error"] = error;
        return meta;
    }
    meta["name"] = def.name;
    meta["model"] = def.modelAsset;
    meta["rigId"] = def.rig.rigId;
    meta["bones"] = def.rig.bones;
    meta["defaultClip"] = def.defaultClip;
    QJsonArray clips;
    for (const auto &clip : def.clips) clips.append(clip.name);
    meta["clips"] = clips;
    return meta;
}

QJsonObject AssetMetadata::computeForSource(int assetType, const QString &sourcePath,
                                            const QString &bakePath, const QString &displayName)
{
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath)) return QJsonObject();
    switch (static_cast<ModelTypes>(assetType)) {
    case ModelTypes::Object:
    case ModelTypes::Mesh: return forModelBake(bakePath, sourcePath);
    case ModelTypes::Texture: return forImageFile(sourcePath);
    case ModelTypes::Music: return forAudioFile(sourcePath);
    case ModelTypes::Video: return forVideoFile(sourcePath);
    case ModelTypes::LightProfile: return forLightProfileFile(sourcePath);
    case ModelTypes::Avatar: return forAvatarFile(sourcePath);
    case ModelTypes::Animation:
        return forClipBake(bakePath, sourcePath, QFileInfo(displayName).completeBaseName());
    default: return forGenericFile(sourcePath);
    }
}

QJsonObject AssetMetadata::ensure(Database *db, const QString &guid, const QString &storeRoot)
{
    if (!db) return QJsonObject();
    const AssetRecord record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) return QJsonObject();

    QJsonObject props = QJsonDocument::fromJson(record.properties).object();
    if (props.contains("metadata")) return props["metadata"].toObject();

    const QString root = storeRoot.isEmpty() ? storeRootPath() : storeRoot;
    // The resolved source object (guid-first — the store is content-addressed).
    const QString source = AssetCas::resolveSource(QSqlDatabase::database(), root, guid);
    QJsonObject meta = computeForSource(record.type, source,
                                        bakePathFor(record.type, source, guid), record.name);
    if (meta.isEmpty()) return meta;   // nothing to describe — don't persist a stub
    // A video block computed off the GUI thread is degraded (no QMediaPlayer
    // there) — hand it back for display but let a GUI-thread call enrich later.
    if (meta["kind"].toString() == "video" && !meta.contains("duration")) return meta;

    props["metadata"] = meta;
    db->updateAssetProperties(guid, QJsonDocument(props).toJson());
    return meta;
}

QString AssetMetadata::storeRootPath()
{
    // Folded into the single path authority (ASSET_PIPELINE_SPEC §3.1.1);
    // kept as a thin alias for its existing callers.
    return AssetStorePaths::root();
}
