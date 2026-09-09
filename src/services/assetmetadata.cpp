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
#include <functional>
#include <limits>
#include <vector>

#include "assimp/Importer.hpp"
#include "assimp/material.h"
#include "assimp/postprocess.h"
#include "irisgl/import/importflags.h"
#include "assimp/scene.h"

#include "data/constants.h"
#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/iesprofile.h"
#include "services/fitsize.h"
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

// The primary file of a store folder for a given extension list, or empty.
QString findByExtension(const QString &folder, const QStringList &exts)
{
    for (const QFileInfo &file : QDir(folder).entryInfoList(QDir::Files, QDir::Name))
        if (exts.contains(file.suffix().toLower()))
            return file.absoluteFilePath();
    return QString();
}

/// The model's axis-aligned WORLD size, in metres — measured on the parsed
/// aiScene, which iris::ImportFlags::Canonical has already unit-converted
/// (aiProcess_GlobalScale). Mesh-local AABBs pushed through each instancing
/// node's accumulated transform: the same shape as the document-side walk in
/// fitsize::measureNode, so the number recorded at import and the number a
/// placed node measures agree.
fitsize::Extent measureSceneExtent(const aiScene *scene)
{
    fitsize::Extent out;
    if (!scene || scene->mNumMeshes == 0 || !scene->mRootNode) return out;

    // Per-mesh local AABB, computed once even when several nodes instance it.
    struct Box { aiVector3D mn, mx; bool any = false; };
    std::vector<Box> boxes(scene->mNumMeshes);
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh *mesh = scene->mMeshes[i];
        if (!mesh || mesh->mNumVertices == 0) continue;
        Box &box = boxes[i];
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            const aiVector3D &p = mesh->mVertices[v];
            if (!box.any) { box.mn = box.mx = p; box.any = true; continue; }
            box.mn.x = std::min(box.mn.x, p.x); box.mx.x = std::max(box.mx.x, p.x);
            box.mn.y = std::min(box.mn.y, p.y); box.mx.y = std::max(box.mx.y, p.y);
            box.mn.z = std::min(box.mn.z, p.z); box.mx.z = std::max(box.mx.z, p.z);
        }
    }

    double lo[3] = { std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                     std::numeric_limits<double>::max() };
    double hi[3] = { -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
                     -std::numeric_limits<double>::max() };
    bool any = false;

    std::function<void(const aiNode *, const aiMatrix4x4 &)> walk =
        [&](const aiNode *node, const aiMatrix4x4 &parent) {
            if (!node) return;
            const aiMatrix4x4 world = parent * node->mTransformation;
            for (unsigned m = 0; m < node->mNumMeshes; ++m) {
                const unsigned index = node->mMeshes[m];
                if (index >= boxes.size() || !boxes[index].any) continue;
                const Box &box = boxes[index];
                for (int c = 0; c < 8; ++c) {
                    aiVector3D corner((c & 1) ? box.mx.x : box.mn.x,
                                      (c & 2) ? box.mx.y : box.mn.y,
                                      (c & 4) ? box.mx.z : box.mn.z);
                    corner *= world;
                    const double p[3] = { corner.x, corner.y, corner.z };
                    for (int a = 0; a < 3; ++a) {
                        lo[a] = std::min(lo[a], p[a]);
                        hi[a] = std::max(hi[a], p[a]);
                    }
                    any = true;
                }
            }
            for (unsigned c = 0; c < node->mNumChildren; ++c) walk(node->mChildren[c], world);
        };
    walk(scene->mRootNode, aiMatrix4x4());

    if (!any) return out;
    out.x = hi[0] - lo[0];
    out.y = hi[1] - lo[1];
    out.z = hi[2] - lo[2];
    out.valid = out.x > 0.0 || out.y > 0.0 || out.z > 0.0;
    return out;
}

/// Metres per source unit AS THE FILE DECLARED IT. FBX is the only format in
/// the set that declares one (GlobalSettings::UnitScaleFactor, documented as
/// CENTIMETRES per unit — a Mixamo download says 1.0); glTF fixes the metre by
/// spec and .obj/.ply/.stl declare nothing, so they read 1. Recorded for the
/// user, never used by the policy: aiProcess_GlobalScale has already applied
/// it to the geometry this measures.
double declaredUnitScale(const aiScene *scene)
{
    if (!scene || !scene->mMetaData) return 1.0;
    double factor = 0.0;
    if (scene->mMetaData->Get("UnitScaleFactor", factor) && factor > 0.0)
        return factor / 100.0;
    float ffactor = 0.0f;
    if (scene->mMetaData->Get("UnitScaleFactor", ffactor) && ffactor > 0.0f)
        return double(ffactor) / 100.0;
    return 1.0;
}

} // namespace

QJsonObject AssetMetadata::forModelScene(const aiScene *scene, const QString &sourceFile)
{
    if (!scene) return QJsonObject();

    qint64 vertices = 0, triangles = 0;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        vertices += scene->mMeshes[i]->mNumVertices;
        triangles += scene->mMeshes[i]->mNumFaces;   // iris triangulates on load
    }

    // Distinct texture references across every material and slot type;
    // embedded textures ("*0" paths) are references too, so a purely
    // embedded model still counts them.
    QSet<QString> texturePaths;
    for (unsigned m = 0; m < scene->mNumMaterials; ++m) {
        for (int t = aiTextureType_DIFFUSE; t <= aiTextureType_UNKNOWN; ++t) {
            const auto type = static_cast<aiTextureType>(t);
            const unsigned count = scene->mMaterials[m]->GetTextureCount(type);
            for (unsigned s = 0; s < count; ++s) {
                aiString path;
                if (scene->mMaterials[m]->GetTexture(type, s, &path) == AI_SUCCESS)
                    texturePaths.insert(QString::fromUtf8(path.C_Str()));
            }
        }
    }
    int textures = texturePaths.size();
    if (textures == 0) textures = static_cast<int>(scene->mNumTextures);

    // ---- THE RIG BLOCK (AVATAR_ASSET_SPEC §5.1) ---------------------------
    //
    // Whether a model is riggable is a property of the FILE, so it belongs in
    // the metadata every import already computes rather than in a second parse
    // the avatar module pays later. It is what `assets.list({rigged:true})`
    // filters on, what "Create Avatar" is offered from, and what the avatar
    // definition's `rig` block is copied from.
    //
    // BONE names come from the meshes' bone lists; NODE names from the scene
    // hierarchy. Both matter: the clip <-> rig join is by SCENE-NODE name
    // (rigsignature.h), and an FBX carries bones assimp never gives a mesh.
    QStringList boneNames;
    QSet<QString> boneSeen;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
        const aiMesh *mesh = scene->mMeshes[i];
        for (unsigned b = 0; b < mesh->mNumBones; ++b) {
            const QString name = QString::fromUtf8(mesh->mBones[b]->mName.C_Str());
            if (name.isEmpty() || boneSeen.contains(name)) continue;
            boneSeen.insert(name);
            boneNames.append(name);
        }
    }

    QStringList nodeNames;
    QSet<QString> nodeSeen;
    std::function<void(const aiNode *)> walk = [&](const aiNode *node) {
        if (!node) return;
        const QString name = QString::fromUtf8(node->mName.C_Str());
        if (!name.isEmpty() && !nodeSeen.contains(name)) {
            nodeSeen.insert(name);
            nodeNames.append(name);
        }
        for (unsigned i = 0; i < node->mNumChildren; ++i) walk(node->mChildren[i]);
    };
    walk(scene->mRootNode);

    // Clips: name, LENGTH IN SECONDS (a file's ticks are meaningless to a
    // reader, and ticksPerSecond is 0 in more exports than not — the same
    // fallback iris::Mesh::extractAnimations uses), and how much of the clip
    // is bone work rather than assimp pivot bookkeeping.
    QJsonArray animations;
    for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
        const aiAnimation *anim = scene->mAnimations[i];
        if (!anim) continue;
        const double tps = anim->mTicksPerSecond > 0.0 ? anim->mTicksPerSecond : 25.0;
        int boneChannels = 0;
        for (unsigned c = 0; c < anim->mNumChannels; ++c) {
            const QString channel = QString::fromUtf8(anim->mChannels[c]->mNodeName.C_Str());
            if (!rig::isPivotChannel(channel)) ++boneChannels;
        }
        QJsonObject clip;
        clip["name"] = QString::fromUtf8(anim->mName.C_Str());
        clip["length"] = anim->mDuration / tps;
        clip["channels"] = static_cast<int>(anim->mNumChannels);
        clip["boneChannels"] = boneChannels;
        animations.append(clip);
    }

    QJsonObject meta;
    meta["kind"] = "model";
    meta["format"] = formatOf(sourceFile);
    meta["fileSize"] = sizeOf(sourceFile);
    meta["vertices"] = vertices;
    meta["triangles"] = triangles;
    meta["meshes"] = static_cast<int>(scene->mNumMeshes);
    meta["materials"] = static_cast<int>(scene->mNumMaterials);
    meta["textures"] = textures;
    meta["hasSkeleton"] = !boneNames.isEmpty();
    meta["bones"] = boneNames.size();
    meta["boneNames"] = QJsonArray::fromStringList(boneNames);
    meta["nodeNames"] = QJsonArray::fromStringList(nodeNames);
    meta["rigId"] = rig::rigId(boneNames);
    meta["animations"] = animations;

    // ---- FIT TO SIZE (services/fitsize.h) ---------------------------------
    //
    // The model's measured size in metres and the fit the size policy infers
    // from it. Computed HERE — the one import-time model-description site —
    // so it also comes for free on the lazy backfill (forModelFile) and needs
    // no migration for rows imported before the feature landed.
    const fitsize::Extent extent = measureSceneExtent(scene);
    fitsize::writeBlock(meta, extent, declaredUnitScale(scene), !boneNames.isEmpty());
    if (meta.contains("fitReason"))
        irisLog(QStringLiteral("import: '%1' %2")
                    .arg(QFileInfo(sourceFile).fileName(),
                         meta.value("fitReason").toString()));
    return meta;
}

QJsonObject AssetMetadata::forModelFile(const QString &filePath)
{
    Assimp::Importer importer;
    // THE canonical preset (ASSET_PIPELINE_SPEC §3.2.2): metadata counts must
    // match the geometry import and every load produce — a third flag set here
    // used to yield vertex/index counts matching neither.
    const aiScene *scene = importer.ReadFile(filePath.toStdString(), iris::ImportFlags::Canonical);
    if (!scene) return forGenericFile(filePath);   // still format/size, never nothing
    return forModelScene(scene, filePath);
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

QJsonObject AssetMetadata::computeForStore(int assetType, const QString &storeFolder)
{
    const QDir dir(storeFolder);
    if (!dir.exists()) return QJsonObject();

    switch (static_cast<ModelTypes>(assetType)) {
    case ModelTypes::Object:
    case ModelTypes::Mesh: {
        const QString model = findByExtension(storeFolder, Constants::MODEL_EXTS);
        if (!model.isEmpty()) return forModelFile(model);
        break;
    }
    case ModelTypes::Texture: {
        const QString image = findByExtension(storeFolder, Constants::IMAGE_EXTS);
        if (!image.isEmpty()) return forImageFile(image);
        break;
    }
    case ModelTypes::Music: {
        const QString audio = findByExtension(storeFolder, Constants::AUDIO_EXTS);
        if (!audio.isEmpty()) return forAudioFile(audio);
        break;
    }
    case ModelTypes::Video: {
        const QString video = findByExtension(storeFolder, Constants::VIDEO_EXTS);
        if (!video.isEmpty()) return forVideoFile(video);
        break;
    }
    case ModelTypes::LightProfile: {
        const QString ies = findByExtension(storeFolder, Constants::LIGHT_PROFILE_EXTS);
        if (!ies.isEmpty()) return forLightProfileFile(ies);
        break;
    }
    case ModelTypes::Avatar: {
        const QString definition = findByExtension(storeFolder, Constants::AVATAR_EXTS);
        if (!definition.isEmpty()) return forAvatarFile(definition);
        break;
    }
    default:
        break;
    }

    // Everything else — shaders, materials, particle systems, skies, files,
    // and typed rows whose expected file is missing: describe the folder.
    const auto files = dir.entryInfoList(QDir::Files, QDir::Size);
    if (files.isEmpty()) return QJsonObject();

    qint64 total = 0;
    for (const QFileInfo &file : files) total += file.size();

    QJsonObject meta;
    meta["kind"] = "file";
    meta["format"] = files.first().suffix().toLower();   // the largest file
    meta["fileSize"] = total;
    if (files.size() > 1) meta["files"] = files.size();
    return meta;
}

QJsonObject AssetMetadata::ensure(Database *db, const QString &guid, const QString &storeRoot)
{
    if (!db) return QJsonObject();
    const AssetRecord record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) return QJsonObject();

    QJsonObject props = QJsonDocument::fromJson(record.properties).object();
    if (props.contains("metadata")) {
        const QJsonObject stored = props["metadata"].toObject();
        // A MODEL block written before the rig fields existed (AVATAR_ASSET
        // §5.1) is incomplete, not absent — and the "metadata exists, stop"
        // short-circuit below would keep it incomplete forever, so
        // `assets.list({rigged:true})` would never see a single library row
        // imported before this build. One extra key is the version marker:
        // recompute once, persist, and every later call is the fast path
        // again. (Ships-as-new-app means no MIGRATIONS; a lazy backfill that
        // already exists for exactly this is not one.)
        // Same story a second time (fit-to-size, 2026-09-09): a model block
        // written before the size policy carries no `fitScale`, so an old
        // library would place every mis-declared model raw forever. `fitScale`
        // joins `hasSkeleton` as a version marker — one recompute, persisted,
        // and the fast path is back.
        if (stored.value("kind").toString() != QLatin1String("model")
            || (stored.contains("hasSkeleton") && stored.contains("fitScale")))
            return stored;
    }

    const QString root = storeRoot.isEmpty() ? storeRootPath() : storeRoot;
    QJsonObject meta = computeForStore(record.type, QDir(root).filePath(guid));
    if (meta.isEmpty()) {
        // CAS-only rows (no per-guid view on disk): describe the resolved
        // source object directly (phase 4 — guid-first resolution).
        const QString source = AssetCas::resolveSource(QSqlDatabase::database(), root, guid);
        if (!source.isEmpty()) {
            switch (static_cast<ModelTypes>(record.type)) {
            case ModelTypes::Object:
            case ModelTypes::Mesh: meta = forModelFile(source); break;
            case ModelTypes::Texture: meta = forImageFile(source); break;
            case ModelTypes::Music: meta = forAudioFile(source); break;
            case ModelTypes::Video: meta = forVideoFile(source); break;
            case ModelTypes::LightProfile: meta = forLightProfileFile(source); break;
            case ModelTypes::Avatar: meta = forAvatarFile(source); break;
            default: meta = forGenericFile(source); break;
            }
        }
    }
    if (meta.isEmpty()) return meta;   // nothing to describe — don't persist a stub
    // A video block computed off the GUI thread is degraded (no QMediaPlayer
    // there) — hand it back for display but let a GUI-thread call enrich later.
    if (meta["kind"].toString() == "video" && !meta.contains("duration")) return meta;

    props["metadata"] = meta;
    db->updateAssetProperties(guid, QJsonDocument(props).toJson());
    return meta;
}

QJsonObject AssetMetadata::writeFit(Database *db, const QString &guid, FitChange change,
                                    double scale, QString *error, const QString &storeRoot)
{
    const auto fail = [error](const QString &message) {
        if (error) *error = message;
        return QJsonObject();
    };
    if (!db) return fail(QStringLiteral("no library is open"));

    const AssetRecord record = db->fetchAsset(guid);
    if (record.guid.isEmpty())
        return fail(QStringLiteral("no asset with guid '%1'").arg(guid));
    if (record.type != static_cast<int>(ModelTypes::Object))
        return fail(QStringLiteral("'%1' is not a model asset \u2014 only models carry a "
                                   "measured size to fit").arg(record.name));
    if (change == FitChange::Manual && (!(scale > 0.0) || !std::isfinite(scale)))
        return fail(QStringLiteral("a fit scale must be a positive number"));

    // ensure() first: it backfills a row that has no block at all, which is
    // what makes Reset and Manual work on a pre-feature library.
    QJsonObject meta = ensure(db, guid, storeRoot);

    if (change == FitChange::Remeasure) {
        const QString root = storeRoot.isEmpty() ? storeRootPath() : storeRoot;
        QJsonObject fresh = computeForStore(record.type, QDir(root).filePath(guid));
        if (fresh.isEmpty()) {
            const QString source = AssetCas::resolveSource(QSqlDatabase::database(), root, guid);
            if (!source.isEmpty()) fresh = forModelFile(source);
        }
        if (fresh.isEmpty())
            return fail(QStringLiteral("'%1' has no stored model file to re-measure")
                            .arg(record.name));
        meta = fresh;
    } else if (change == FitChange::Manual) {
        fitsize::writeOverride(meta, scale);
    } else {
        fitsize::writeBlock(meta, fitsize::extentOf(meta),
                            meta.value(QStringLiteral("unitScale")).toDouble(1.0),
                            meta.value(QStringLiteral("hasSkeleton")).toBool());
    }

    QJsonObject props = QJsonDocument::fromJson(record.properties).object();
    props["metadata"] = meta;
    if (!db->updateAssetProperties(guid, QJsonDocument(props).toJson()))
        return fail(QStringLiteral("could not write '%1'").arg(record.name));
    return meta;
}

QString AssetMetadata::storeRootPath()
{
    // Folded into the single path authority (ASSET_PIPELINE_SPEC §3.1.1);
    // kept as a thin alias for its existing callers.
    return AssetStorePaths::root();
}
