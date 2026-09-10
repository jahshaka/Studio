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
namespace iris { struct ModelSceneInfo; }

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

    // Backfill path: one canonical parse of the file (iris::ModelSceneInfo::read
    // — no GPU, no iris document).
    static QJsonObject forModelFile(const QString &filePath);

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
    /// fit that rig" is a string compare between two rows. Read by
    /// animfile::read (one parse, names and numbers only).
    static QJsonObject forAnimationFile(const QString &filePath);

    /// The avatar DEFINITION block (AVATAR_ASSET_SPEC §3.1): what the tile and
    /// the module's library list show without opening the avatar — its name,
    /// the model Object it instantiates, its rig id and bone count, and its
    /// clip names. Read from the JSON itself, so it is exactly as current as
    /// the version this row's `source` points at.
    static QJsonObject forAvatarFile(const QString &filePath);

    // Dispatches on the asset row's ModelTypes over its store folder
    // (AssetStore/<guid>/). Returns an empty object when the folder holds
    // nothing to describe (e.g. reserved built-ins with no files).
    static QJsonObject computeForStore(int assetType, const QString &storeFolder);

    // The lazy backfill: returns properties["metadata"], computing and
    // persisting it when absent. storeRoot is overridable for tests;
    // empty = the real AssetStore.
    static QJsonObject ensure(Database *db, const QString &guid,
                              const QString &storeRoot = QString());

    // ---- FIT TO SIZE (services/fitsize.h) ---------------------------------
    //
    // The ONE write behind `assets.setFit` and the Assets page's "Imported
    // size" row, so a scripted change and a clicked one cannot diverge.
    // Manual    record `scale` as the asset's fit (fitSource "manual")
    // Reset     throw the manual override away, recompute the automatic fit
    //           from the recorded extent
    // Remeasure re-measure the model from its stored source file and
    //           recompute everything from that (the pre-feature library case)
    enum class FitChange { Manual, Reset, Remeasure };

    /// Returns the resulting metadata block, or an empty object with `error`
    /// set. `storeRoot` is overridable for tests; empty = the real AssetStore.
    static QJsonObject writeFit(Database *db, const QString &guid, FitChange change,
                                double scale, QString *error,
                                const QString &storeRoot = QString());

    static QString storeRootPath();
};

#endif // ASSETMETADATA_H
