/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// Link stubs for commands.structural_undo (the tests/ui/test_stubs.cpp idiom).
//
// The undo commands raise UI refreshes through SceneEditService and
// SelectionService and delete an asset row through Database. Linking the real
// ones would drag the whole io/ layer, the asset store and Qt Sql into a suite
// whose subject is pointer bookkeeping — and, worse, would make the FRAGMENT
// pair (capture/rebuild) real, so the suite could not choose which branch of
// structuralundo::reinstate to exercise.
//
// So: the refresh notifications do nothing (the app connects them to panels
// that do not exist here), the capture returns a fragment carrying just the
// node's identity (which is exactly what `liveIsUsable` checks), and the
// rebuild hands back whatever the suite armed.

#include "test_stubs.h"

#include "data/database/database.h"
#include "io/scenereader.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"

namespace teststubs
{
iris::SceneNodePtr nextRebuild;

SceneEditService *sceneEditService()
{
    static SceneEditService *instance =
        new SceneEditService(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
    return instance;
}
} // namespace teststubs

// ---- SceneEditService ------------------------------------------------------

SceneEditService::SceneEditService(Database *db, Project *project, UndoService *undo,
                                   SelectionService *selection, IEditorViewport *viewport,
                                   std::function<iris::ScenePtr()> sceneProvider, QObject *parent)
    : QObject(parent), db(db), project(project), undo(undo), selection(selection),
      viewport(viewport), sceneProvider(std::move(sceneProvider))
{
}

void SceneEditService::notifyNodeInserted(const iris::SceneNodePtr &) {}
void SceneEditService::notifyNodeRemoved(const iris::SceneNodePtr &) {}
void SceneEditService::notifyHierarchyChanged() {}
void SceneEditService::notifyTransformChanged() {}

SceneFragment SceneEditService::captureFragment(const iris::SceneNodePtr &node) const
{
    SceneFragment fragment;
    if (!node) return fragment;
    // Identity only — enough for liveIsUsable's check, and it keeps the suite
    // out of the serializer. The REAL capture is proven end-to-end by
    // scripting.e2e.fragments, which runs the app's own writer and reader.
    fragment.node.insert(QStringLiteral("guid"), node->getGUID());
    fragment.nodeIds.append(node->getNodeId());
    return fragment;
}

iris::SceneNodePtr SceneEditService::rebuildFragment(const SceneFragment &) const
{
    auto out = teststubs::nextRebuild;
    teststubs::nextRebuild.reset();
    return out;
}

// ---- SelectionService ------------------------------------------------------

void SelectionService::select(iris::SceneNodePtr) {}

// ---- Database --------------------------------------------------------------

bool Database::deleteAsset(const QString &) { return false; }
