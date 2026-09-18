/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/materialpreviewservice.h"

#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/materials/material.h"

#include "services/sceneeditservice.h"

namespace {

iris::SceneNodePtr findByGuid(const iris::SceneNodePtr &node, const QString &guid)
{
    if (!node) return iris::SceneNodePtr();
    if (node->getGUID() == guid) return node;
    for (const auto &child : node->children())
        if (auto hit = findByGuid(child, guid)) return hit;
    return iris::SceneNodePtr();
}

} // namespace

MaterialPreviewService::MaterialPreviewService(SceneEditService *sceneEdit)
    : mSceneEdit(sceneEdit)
{
}

MaterialPreviewService::~MaterialPreviewService()
{
    // A preview that outlived its owner would leave a borrowed material on a
    // mesh and the scene's flag raised for ever.
    end();
}

QString MaterialPreviewService::nodeGuid() const
{
    return active() ? mNode->getGUID() : QString();
}

bool MaterialPreviewService::canPreview(const QString &presetOrGuid)
{
    if (!mSceneEdit || presetOrGuid.isEmpty()) return false;
    if (presetOrGuid == mResolvedSource) return !mResolved.isNull();
    // Resolving to answer the question is the point: the answer IS the
    // material, and the gesture that asked will want it a moment later.
    mResolved = mSceneEdit->resolveMaterial(presetOrGuid);
    mResolvedSource = presetOrGuid;
    return !mResolved.isNull();
}

bool MaterialPreviewService::begin(const QString &nodeGuid, const QString &presetOrGuid)
{
    const iris::ScenePtr scene = mSceneEdit ? mSceneEdit->scene() : iris::ScenePtr();
    if (!scene) { end(); return false; }
    return begin(findByGuid(scene->getRootNode(), nodeGuid), presetOrGuid);
}

bool MaterialPreviewService::begin(const iris::SceneNodePtr &node, const QString &presetOrGuid)
{
    if (!mSceneEdit) return false;

    // Nothing to show on: end whatever is running and say so. This is the
    // "the cursor moved onto the sky" case and it must restore, not leak —
    // two of the viewport's old early-outs cleared the preview state WITHOUT
    // putting the material back, which left the borrowed material on the mesh
    // with no undo step and no record anywhere that it was ever there.
    if (!node || node->getSceneNodeType() != iris::SceneNodeType::Mesh) { end(); return false; }

    // A LOCKED NODE GETS NO PREVIEW (owner, 2026-09-15): the drop will refuse
    // it by name, and a preview on a node that will refuse is a promise the
    // release cannot keep.
    if (!node->isPickable()) { end(); return false; }

    if (!canPreview(presetOrGuid)) { end(); return false; }

    auto meshNode = node.staticCast<iris::MeshNode>();
    if (active() && mNode == meshNode && mSource == presetOrGuid) return true;

    // Moving to a different node (or a different payload on the same one):
    // the previous node gets its own material back first, always.
    end();

    mNode = meshNode;
    mOriginal = meshNode->getMaterial();
    mSource = presetOrGuid;
    mScene = mSceneEdit->scene();
    markScene(true);
    meshNode->setMaterial(mResolved);
    return true;
}

bool MaterialPreviewService::end()
{
    if (!active()) { mScene.reset(); return false; }

    // THE EXACT ORIGINAL POINTER, not a re-resolution of it: the node may have
    // carried a material nothing else in the process can rebuild (an imported
    // model's, an in-place edit's).
    mNode->setMaterial(mOriginal);
    mNode.reset();
    mOriginal.reset();
    mSource.clear();
    markScene(false);
    mScene.reset();
    // The resolve cache deliberately SURVIVES: a drag that leaves one object
    // and enters the next ends and begins, and re-parsing the same material
    // for every object crossed is the cost this cache exists to remove.
    return true;
}

void MaterialPreviewService::markScene(bool on)
{
    if (!mScene) return;
    if (on) {
        ++mScene->materialPreviewDepth;
    } else if (mScene->materialPreviewDepth > 0) {
        --mScene->materialPreviewDepth;
    }
}
