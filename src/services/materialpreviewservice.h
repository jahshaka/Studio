/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALPREVIEWSERVICE_H
#define MATERIALPREVIEWSERVICE_H

#include <QString>

#include "irisgl/irisglfwd.h"

class SceneEditService;

/// MaterialPreviewService — THE LIVE HOVER PREVIEW (MATERIAL-PREVIEW-1).
///
/// Dragging a material over the scene shows it on the object under the cursor
/// and follows the cursor to the next one. The owner found it working for
/// materials made from an image and not for the two other sources, and wanted
/// it for all of them ("very very cool, we want this").
///
/// It was never three behaviours: there is ONE runtime material class and all
/// three sources send the same drag payload. The preview lived inline in the
/// viewport's dragMove handler and read the dragged material out of a QVariant
/// on the AssetManager — a QVariant three of the registration sites had filled
/// with `QSharedPointer<PbrMaterial>` through `auto` while the reader asked for
/// `iris::MaterialPtr`. Qt has no converter between the two (Material is not a
/// QObject), so the read was null and the preview was silently skipped for
/// every tray preset and every graph material. The drop re-resolved the guid
/// from the database and worked, which is why it looked like a preview problem
/// and not a resolution one.
///
/// So the preview is a SERVICE over the ONE resolver
/// (SceneEditService::resolveMaterial), with verbs (`material.preview`,
/// `material.endPreview`) like any other editor capability, and the viewport's
/// three drag handlers became calls into it.
///
/// THE INVARIANTS, each of which was a defect before it:
///
///   * THE MATERIAL IS PRIVATE. `MeshNode::setMaterial` mutates the instance it
///     is handed (SKINNING_ENABLED and friends), so the preview never borrows
///     a shared one — resolveMaterial always builds a fresh instance, and this
///     service holds it for the life of the gesture.
///   * IT IS RESOLVED ONCE PER GESTURE, keyed on the payload string, so moving
///     across twenty objects costs one parse and twenty slot writes.
///   * IT NEVER TOUCHES THE DOCUMENT. No undo push, no dirty flag, no database
///     row. `end()` puts the exact original material pointer back.
///   * IT ALWAYS ENDS. Drop, drag-leave, Escape/cancel, scene close, project
///     save, space switch and any undo push end it first — a save during a
///     preview writes the ORIGINAL, and an undo command captures the original
///     rather than the borrowed material.
///   * A HOVER COSTS NO GI. While a preview is live the scene says so
///     (`iris::Scene::materialPreviewDepth`) and the mirror's GI debounce
///     neither arms nor adopts the material term — measured: a hover used to
///     buy a full re-solve on the way in and another on the way out.
///
/// QObject-free, like UndoService and for the same reason: it is constructed in
/// headless hosts and driven by tests that link no widgets.
class MaterialPreviewService
{
public:
    /// `sceneEdit` owns the resolver and the scene accessor; not owned, and it
    /// outlives this service (both belong to the shell).
    explicit MaterialPreviewService(SceneEditService *sceneEdit);
    ~MaterialPreviewService();

    MaterialPreviewService(const MaterialPreviewService &) = delete;
    MaterialPreviewService &operator=(const MaterialPreviewService &) = delete;

    /// Shows `presetOrGuid` on `node`, ending any preview on a different node
    /// first. False — and no preview left running — when the payload resolves
    /// to nothing, the node is not a mesh, or the node is LOCKED (a preview on
    /// a node whose drop will be refused is a promise the release cannot keep).
    /// Re-calling with the same node and payload is a cheap no-op.
    bool begin(const iris::SceneNodePtr &node, const QString &presetOrGuid);

    /// The verb's shape: the node named by GUID in the open scene.
    bool begin(const QString &nodeGuid, const QString &presetOrGuid);

    /// ENDS THE GESTURE: puts the original material back and drops the
    /// per-gesture resolve cache, so a material edited between two drags is
    /// shown as it is now. True when a preview was running. Safe — and free —
    /// with none.
    bool end();

    bool active() const { return !mNode.isNull(); }
    /// The node the preview is on — the DROP TARGET, which is the node under
    /// the cursor and never the selection. Null when no preview is running.
    iris::MeshNodePtr node() const { return mNode; }
    /// The previewed node's guid, empty when no preview is running.
    QString nodeGuid() const;
    /// The payload string the live preview is showing, empty when none.
    QString source() const { return active() ? mSource : QString(); }

    /// Does `presetOrGuid` name something this build can show? The drag is
    /// ACCEPTED on this answer, so the cursor is honest before the release
    /// rather than after it (a Shader tile with no baked material is refused
    /// visibly instead of being accepted and doing nothing).
    bool canPreview(const QString &presetOrGuid);

private:
    /// The restore half of `end`, WITHOUT dropping the resolve cache: what
    /// `begin` does when the hover moves from one object to the next inside one
    /// gesture. Never called from outside — a caller that means "the gesture is
    /// over" means `end`.
    bool restore();

    /// Raises/lowers the scene's preview flag, which is what keeps the GI
    /// debounce out of a state that will never be committed.
    void markScene(bool on);

    SceneEditService *mSceneEdit = nullptr;

    // The live preview.
    iris::MeshNodePtr mNode;
    iris::MaterialPtr mOriginal;
    iris::ScenePtr    mScene;        ///< the scene the flag was raised on
    QString           mSource;

    // The per-gesture resolve cache (keyed on the payload string).
    QString           mResolvedSource;
    iris::MaterialPtr mResolved;
};

#endif // MATERIALPREVIEWSERVICE_H
