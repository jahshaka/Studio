/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SKYASSETDEFINITION_H
#define SKYASSETDEFINITION_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"

/// Applying a SKY LIBRARY ASSET's stored definition to a scene document.
///
/// A sky asset's data blob is the same per-type block the scene format stores
/// (`SceneReader::readScene`'s sky switch reads the identical keys) — the only
/// difference is where its textures come from: a sky asset names TEXTURE ASSET
/// GUIDS, which the caller resolves through the asset store, while a project
/// scene names project-relative paths.
///
/// This exists because the Assets-page preview (`EngineAssetViewer::applyJafSky`)
/// used to open-code a partial copy of that switch and simply DROPPED the
/// realistic and cubemap cases behind a stale "not on the engine yet" comment —
/// so those two sky types previewed as a flat background even though the mirror
/// has rendered both for months (VISUAL_PARITY re-audit F1). One function, no
/// Database dependency (the guid→path step is the caller's lambda), so a test
/// can drive exactly what the viewer drives.
namespace skyassets
{

/// Resolves a texture ASSET guid to an absolute file path on disk.
/// Return an empty string when the guid is unknown or its bytes are missing.
using TexturePathResolver = std::function<QString(const QString &guid)>;

/// Writes `skyData` (one sky asset's stored definition, of type `type`) into
/// `scene`'s live sky fields — exactly the fields SceneMirror::applySky polls.
///
/// `textureDependees` is the asset's Texture dependee guids, used only as the
/// fallback for an equirect definition written before the `equiSkyGuid` key
/// existed. Returns false when the definition could not be honoured (a
/// texture-backed sky whose textures do not resolve); the scene's `skyType` is
/// set either way, so a caller that ignores the result still gets the "no sky"
/// behaviour rather than a stale one.
bool applyToScene(const iris::ScenePtr &scene, iris::SkyType type,
                  const QJsonObject &skyData,
                  const TexturePathResolver &resolve,
                  const QStringList &textureDependees = QStringList());

}

#endif // SKYASSETDEFINITION_H
