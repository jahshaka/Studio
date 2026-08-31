/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef GLTFEXPORTER_H
#define GLTFEXPORTER_H

// GltfExporter — hand-rolled glTF 2.0 (GLB) writer from the iris document model
// (WEB_EXPORT_AUDIT §2 option (b)). Pure document consumer: no Ogre, no GL, no
// assimp — mesh buffers are read straight from iris::Mesh::getVertexBuffers(),
// materials from PbrMaterial/DefaultMaterial/CustomMaterial, lights/cameras/
// particles from their nodes, and everything glTF cannot say natively rides in
// the `jah.*` extras sidecar (GLTFLoader maps extras to Object3D.userData).
//
// Coverage follows the audit's §1 table:
//   core: node tree/TRS, meshes (+generated float4 tangents), PBR scalars and
//   textures (metal/rough channel-packed, roughness remap baked), cameras,
//   skins + skeletal animations (phase 2).
//   KHR: lights_punctual (-Y-to--Z orientation shim node), texture_transform,
//   emissive_strength, transmission (glass).
//   extras: sky (gradient baked to an equirect strip, cubemap stitched to
//   equirect), fog, per-light shadow settings, area lights, particles,
//   ambient audio, viewpoints, GI mode (informational).

#include <QByteArray>
#include <QJsonObject>
#include <QStringList>

#include "irisgl/irisglfwd.h"

class GltfExporter
{
public:
    struct Result
    {
        bool ok = false;
        QString error;
        QByteArray glb;          // complete .glb bytes (JSON + BIN chunks)
        QJsonObject json;        // the glTF JSON document (tests introspect this)
        QStringList warnings;
        QStringList extensionsUsed;
        int nodeCount = 0;
        int meshCount = 0;
        int materialCount = 0;
        int lightCount = 0;
        int cameraCount = 0;
        int animationCount = 0;
        int skinCount = 0;
        QString audioSourcePath; // ambient audio file to copy beside the export, if any
    };

    /// Converts the live document scene to a binary glTF. `sceneName` becomes
    /// the glTF scene name. Never throws; failures land in Result::error.
    static Result exportScene(const iris::ScenePtr &scene, const QString &sceneName);
};

#endif // GLTFEXPORTER_H
