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
#include <functional>

#include <QString>

class Database;
namespace iris { struct ModelSceneInfo; }
namespace animfile { struct Contents; }

// Rich per-type asset metadata (ASSET_DRAWERS_SPEC.md addendum).
//
// Lives inside the assets table's `properties` JSON under the "metadata" key
// (beside the viewer's "camera" object for models). Computed at import time
// from data the import already has (the parsed model scene, the image header,
// the wav header) and lazily backfilled for pre-existing library rows the first
// time they are inspected — assets.metadata(guid) or selecting the tile.
//
// Everything here is pure file/scene inspection: no GPU, no engine, no Qt
// widgets — safe to run on a QtConcurrent worker thread. Only ensure() talks
// to the Database (call it from the thread that owns the connection).
// EXCEPTION — video (ASSET_MEDIA_SPEC §1): the rich fields come from
// QMediaPlayer (ffmpeg backend), which is GUI-thread-only; forVideoFile
// degrades to format/fileSize on a worker thread, and ensure() refuses to
// persist that degraded block so a later GUI-thread call can still enrich.
//
// Blocks by kind (every block: format = lowercase source extension,
// fileSize = bytes of the primary file):
//   model: vertices, triangles, meshes, materials, textures, and the RIG
//          block (AVATAR_ASSET_SPEC §5.1) — hasSkeleton, bones, boneNames,
//          nodeNames, rigId (rigsignature.h: a stable hash of the sorted bone
//          names) and animations:[{name, length (seconds), channels,
//          boneChannels}]. Whether a model can be an avatar is a property of
//          the FILE, so it is computed once by the import everyone already
//          pays rather than by a second parse per module
//   image: width, height
//   audio: duration (ms), sampleRate, channels, bitsPerSample (wav only —
//          other containers get format/fileSize)
//   video: duration (ms), width, height, frameRate, videoCodec (whatever
//          the container reports; GUI thread only — see above)
//   file:  format/fileSize only (shaders, materials, skies, particles, misc)
//   animation: clips:[{name, rawName, length (seconds), channels,
//          boneChannels}], duration, boneNames, bones, rigId — the clip's own
//          rig signature, so a clip advertises which rigs it fits
//   lightprofile: verticalAngles, horizontalAngles, coneType, peakCandela,
//          lumensPerLamp, inputWatts, normalisationFactor, manufacturer,
//          luminaire (IesProfile::metadata)
class AssetMetadata
{
public:
    // Model stats from the facts of a scene an import already parsed
    // (iris::ModelSceneInfo::fromSource — the canonical parse triangulates,
    // so its face count is the triangle count).
    static QJsonObject forModelScene(const iris::ModelSceneInfo &scene, const QString &sourceFile);

    // ---- THE BACKFILL READS BAKES (SHIPPED-BAKES-1) ------------------------
    //
    // A library row whose block is absent is described from its BAKE — the
    // model bake carries the facts of the parse it was built from
    // (MeshBake::Model::describe), the clip bake its clip table and poses — and
    // never from a parse: assimp is an import-time dependency
    // (`source.assimp_import_only`). A row with no current bake describes as
    // nothing (not persisted); the library's background rebuild makes the bake
    // and the next inspection describes it.

    /// A model row's block from its current bake at `bakePath`.
    static QJsonObject forModelBake(const QString &bakePath, const QString &sourceFile);

    /// A clip row's block from its current clip bake at `bakePath`. `baseName`
    /// names a clip whose own name is junk (the ROW's base name, never a hash).
    static QJsonObject forClipBake(const QString &bakePath, const QString &sourceFile,
                                   const QString &baseName);

    /// WHERE A ROW'S CURRENT BAKE IS, as a seam: the lookup is a catalog query
    /// that lives in MeshBakeStore, and this service is linked on its own into
    /// a dozen small suites with no catalog — so the app sets it once
    /// (src/app/main.cpp) and everywhere else there is simply no bake.
    /// Database thread.
    using BakePathResolver = std::function<QString(int assetType, const QString &sourcePath,
                                                   const QString &assetGuid)>;
    static void setBakePathResolver(BakePathResolver resolver);
    static QString bakePathFor(int assetType, const QString &sourcePath, const QString &assetGuid);

    static QJsonObject forImageFile(const QString &filePath);   // header-only decode
    static QJsonObject forAudioFile(const QString &filePath);   // RIFF parse for wav
    static QJsonObject forVideoFile(const QString &filePath);   // QMediaPlayer probe (GUI thread)
    static QJsonObject forGenericFile(const QString &filePath);
    /// IES photometric profile: angle counts, cone type, peak candela and the
    /// `normalisationFactor` the mirror divides light intensity by so that
    /// binding a profile changes the falloff's SHAPE and not its brightness.
    static QJsonObject forLightProfileFile(const QString &filePath);

    /// ANIMATION CLIP FILES (ModelTypes::Animation): the clip table
    /// [{name, rawName, length (seconds), channels, boneChannels}], the bone
    /// names the channels DRIVE and the `rigId` hashed over them — the same
    /// hash the model side computes over its bone names, so "does this clip
    /// fit that rig" is a string compare between two rows. Built from contents
    /// already read (the import's clip bake, or forClipBake's).
    static QJsonObject forAnimationContents(const animfile::Contents &contents,
                                            const QString &filePath);

    /// The avatar DEFINITION block (AVATAR_ASSET_SPEC §3.1): what the tile and
    /// the module's library list show without opening the avatar — its name,
    /// the model Object it instantiates, its rig id and bone count, and its
    /// clip names. Read from the JSON itself, so it is exactly as current as
    /// the version this row's `source` points at.
    static QJsonObject forAvatarFile(const QString &filePath);

    // Dispatches on the asset row's ModelTypes over its RESOLVED source file
    // (AssetCas::resolveSource) and, for a model or a clip, its resolved BAKE
    // (`bakePath`, bakePathFor — resolve it on the database thread). Empty when
    // there is nothing to describe (no stored bytes, or a model/clip with no
    // current bake). Pure file inspection: safe on a worker, bar Video
    // (QMediaPlayer — GUI thread). `displayName` is the row's name.
    static QJsonObject computeForSource(int assetType, const QString &sourcePath,
                                        const QString &bakePath = QString(),
                                        const QString &displayName = QString());

    // The lazy backfill: returns properties["metadata"], computing and
    // persisting it when absent. storeRoot is overridable for tests;
    // empty = the real AssetStore.
    static QJsonObject ensure(Database *db, const QString &guid,
                              const QString &storeRoot = QString());

    static QString storeRootPath();
};

#endif // ASSETMETADATA_H
