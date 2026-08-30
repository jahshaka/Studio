/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETMETADATA_H
#define ASSETMETADATA_H

#include <QJsonObject>
#include <QString>

class Database;
struct aiScene;

// Rich per-type asset metadata (ASSET_DRAWERS_SPEC.md addendum).
//
// Lives inside the assets table's `properties` JSON under the "metadata" key
// (beside the viewer's "camera" object for models). Computed at import time
// from data the import already has (the assimp scene, the image header, the
// wav header) and lazily backfilled for pre-existing library rows the first
// time they are inspected — assets.metadata(guid) or selecting the tile.
//
// Everything here is pure file/aiScene inspection: no GPU, no engine, no Qt
// widgets — safe to run on a QtConcurrent worker thread. Only ensure() talks
// to the Database (call it from the thread that owns the connection).
// EXCEPTION — video (ASSET_MEDIA_SPEC §1): the rich fields come from
// QMediaPlayer (ffmpeg backend), which is GUI-thread-only; forVideoFile
// degrades to format/fileSize on a worker thread, and ensure() refuses to
// persist that degraded block so a later GUI-thread call can still enrich.
//
// Blocks by kind (every block: format = lowercase source extension,
// fileSize = bytes of the primary file):
//   model: vertices, triangles, meshes, materials, textures
//   image: width, height
//   audio: duration (ms), sampleRate, channels, bitsPerSample (wav only —
//          other containers get format/fileSize)
//   video: duration (ms), width, height, frameRate, videoCodec (whatever
//          the container reports; GUI thread only — see above)
//   file:  format/fileSize only (shaders, materials, skies, particles, misc)
class AssetMetadata
{
public:
    // Model stats from an assimp scene an import already loaded (iris loads
    // triangulated, so aiMesh faces are triangles).
    static QJsonObject forModelScene(const aiScene *scene, const QString &sourceFile);

    // Backfill path: loads the model with a private Assimp importer
    // (triangulate only — assimp-light, no GPU, no iris document).
    static QJsonObject forModelFile(const QString &filePath);

    static QJsonObject forImageFile(const QString &filePath);   // header-only decode
    static QJsonObject forAudioFile(const QString &filePath);   // RIFF parse for wav
    static QJsonObject forVideoFile(const QString &filePath);   // QMediaPlayer probe (GUI thread)
    static QJsonObject forGenericFile(const QString &filePath);

    // Dispatches on the asset row's ModelTypes over its store folder
    // (AssetStore/<guid>/). Returns an empty object when the folder holds
    // nothing to describe (e.g. reserved built-ins with no files).
    static QJsonObject computeForStore(int assetType, const QString &storeFolder);

    // The lazy backfill: returns properties["metadata"], computing and
    // persisting it when absent. storeRoot is overridable for tests;
    // empty = the real AssetStore.
    static QJsonObject ensure(Database *db, const QString &guid,
                              const QString &storeRoot = QString());

    static QString storeRootPath();
};

#endif // ASSETMETADATA_H
