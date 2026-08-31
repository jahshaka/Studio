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
#include <QSet>
#include <QStandardPaths>
#include <QtEndian>

#include "assimp/Importer.hpp"
#include "assimp/material.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "data/constants.h"
#include "data/database/database.h"
#include "services/assetstorepaths.h"
#include "data/project.h"
#include "irisgl/core/irisutils.h"
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

    QJsonObject meta;
    meta["kind"] = "model";
    meta["format"] = formatOf(sourceFile);
    meta["fileSize"] = sizeOf(sourceFile);
    meta["vertices"] = vertices;
    meta["triangles"] = triangles;
    meta["meshes"] = static_cast<int>(scene->mNumMeshes);
    meta["materials"] = static_cast<int>(scene->mNumMaterials);
    meta["textures"] = textures;
    return meta;
}

QJsonObject AssetMetadata::forModelFile(const QString &filePath)
{
    Assimp::Importer importer;
    // Triangulate only: counts must match what the import-time path saw
    // (iris loads triangulated), and nothing else here needs post-processing.
    const aiScene *scene = importer.ReadFile(filePath.toStdString(), aiProcess_Triangulate);
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
    if (props.contains("metadata")) return props["metadata"].toObject();

    const QString root = storeRoot.isEmpty() ? storeRootPath() : storeRoot;
    const QJsonObject meta = computeForStore(record.type, QDir(root).filePath(guid));
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
