/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "export/walkers/scenewalker.h"

#include "export/walkers/materialtexturereader.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/decalnode.h"
#include "irisgl/document/materials/material.h"
#include "irisgl/document/assets/texture2d.h"

namespace exportwalk {

NodeKind classifyNode(const iris::SceneNodePtr &node)
{
    if (!node) return NodeKind::Empty;
    switch (node->getSceneNodeType()) {
    case iris::SceneNodeType::Mesh:           return NodeKind::Mesh;
    case iris::SceneNodeType::Light:          return NodeKind::Light;
    case iris::SceneNodeType::ParticleSystem: return NodeKind::ParticleSystem;
    case iris::SceneNodeType::Viewer:         return NodeKind::Viewer;
    case iris::SceneNodeType::Decal:          return NodeKind::Decal;
    // CAMERAS_SPEC phase 1: the CameraNode constructor sets its own type now,
    // so cameras classify off the enum like every other kind. The dynamic_cast
    // that used to be needed here is gone.
    case iris::SceneNodeType::Camera:         return NodeKind::Camera;
    default: break;
    }
    return NodeKind::Empty;
}

bool shouldSkipForExport(const iris::SceneNodePtr &node)
{
    return node->getSceneNodeType() == iris::SceneNodeType::Mesh && !node->isExportable();
}

namespace {

int walkNode(const iris::SceneNodePtr &node, const NodeVisitor &visit)
{
    // childCount()/childAt(): the walk reads the one tree directly instead of
    // building a QList of shared pointers per node (SCENEGRAPH_SPEC §3 step 5).
    QVector<int> childHandles;
    const int kids = node->childCount();
    childHandles.reserve(kids);
    for (int i = 0; i < kids; ++i) {
        iris::SceneNode *child = node->childAt(i);
        if (!child) continue;
        const iris::SceneNodePtr ptr = child->sharedFromThis();
        if (shouldSkipForExport(ptr)) continue;
        const int h = walkNode(ptr, visit);
        if (h >= 0) childHandles.append(h);
    }
    return visit(node, childHandles);
}

} // namespace

QVector<int> walkScene(const iris::ScenePtr &scene, const NodeVisitor &visit)
{
    QVector<int> roots;
    if (!scene || !scene->rootNode) return roots;
    const int kids = scene->rootNode->childCount();
    for (int i = 0; i < kids; ++i) {
        iris::SceneNode *child = scene->rootNode->childAt(i);
        if (!child) continue;
        const iris::SceneNodePtr ptr = child->sharedFromThis();
        if (shouldSkipForExport(ptr)) continue;
        const int h = walkNode(ptr, visit);
        if (h >= 0) roots.append(h);
    }
    return roots;
}

SceneInventory collectInventory(const iris::ScenePtr &scene)
{
    SceneInventory inv;
    if (!scene) return inv;

    auto addTextureSource = [&inv](const QString &source) {
        if (!source.isEmpty() && !inv.textureSources.contains(source))
            inv.textureSources.append(source);
    };

    walkScene(scene, [&](const iris::SceneNodePtr &node, const QVector<int> &) -> int {
        ++inv.totalNodes;
        switch (classifyNode(node)) {
        case NodeKind::Mesh: {
            ++inv.meshNodes;
            auto *meshNode = static_cast<iris::MeshNode *>(node.data());
            iris::Material *mat = meshNode->getMaterial().data();
            if (mat && !inv.materials.contains(mat)) {
                inv.materials.append(mat);
                for (const TextureSlot &slot : materialTextureSlots(mat))
                    addTextureSource(slot.source);
            }
            break;
        }
        case NodeKind::Light:          ++inv.lights; break;
        case NodeKind::Camera:         ++inv.cameras; break;
        case NodeKind::ParticleSystem: {
            ++inv.particleSystems;
            auto *ps = static_cast<iris::ParticleSystemNode *>(node.data());
            if (ps->texture) addTextureSource(ps->texture->source);
            break;
        }
        case NodeKind::Viewer:         ++inv.viewers; break;
        case NodeKind::Decal: {
            ++inv.decals;
            // A decal's image is a real texture dependency: packaging and the
            // raw exporter must carry it even though nothing renders it as a
            // material map.
            auto *decal = static_cast<iris::DecalNode *>(node.data());
            addTextureSource(decal->resolvedTexturePath);
            addTextureSource(decal->resolvedNormalPath);
            addTextureSource(decal->resolvedEmissivePath);
            break;
        }
        case NodeKind::Empty:          ++inv.empties; break;
        }
        return 0;
    });

    // File-backed skies count as texture dependencies too.
    if (scene->skyType == iris::SkyType::EQUIRECTANGULAR && scene->skyTexture)
        addTextureSource(scene->skyTexture->source);
    if (scene->skyType == iris::SkyType::CUBEMAP)
        for (int i = 0; i < 6; ++i) addTextureSource(scene->skyBoxTextures[i]);

    return inv;
}

} // namespace exportwalk
