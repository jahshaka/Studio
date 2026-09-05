/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEFOLDERCOMMAND_H
#define SCENEFOLDERCOMMAND_H

// One undo step for one outliner-folder gesture (SCENEGRAPH_SPEC §6b).
//
// Folder state is small, purely metadata, and every interesting operation
// touches an unbounded set of it — renaming "Props" re-paths every sub-folder
// and every member; deleting it lifts them all one level; folder-ising a
// multi-selection writes N nodes. A per-operation inverse would be four
// bespoke, individually-wrong-able implementations of the same idea, so this
// command SNAPSHOTS the whole folder state instead (scenefolders::Snapshot:
// the explicit list plus guid -> path for the members). Correct by
// construction, and cheap: a few dozen short strings.
//
// Contract, same shape as NodeEditCommand's:
//   * the caller snapshots BEFORE the edit, applies it, and pushes this after —
//     a refused edit therefore never lands an undo entry;
//   * QUndoStack::push() replays redo() immediately, which is a re-apply of the
//     "after" snapshot the caller has already produced: idempotent by
//     definition, because a snapshot restore is not an operation;
//   * the scene is held weakly. A folder undo after the project closed is a
//     no-op rather than a resurrection.

#include <QPointer>

#include "commands/studiocommand.h"
#include "services/scenefolders.h"

#include "irisgl/irisglfwd.h"

class SceneHierarchyWidget;

class SceneFolderCommand : public StudioCommand
{
public:
    /// `before` must be the snapshot taken before the edit; the "after" state is
    /// snapshotted from the live scene here, so construct this AFTER applying.
    SceneFolderCommand(const QString &text, const iris::ScenePtr &scene,
                       const scenefolders::Snapshot &before);

    void undo() override;
    void redo() override;

    /// Optional: the panel to repopulate after an undo/redo (folder rows are a
    /// pure rebuild). Null in headless/script runs.
    void setPanel(SceneHierarchyWidget *panel);

private:
    void apply(const scenefolders::Snapshot &snap);

    iris::SceneWPtr mScene;
    scenefolders::Snapshot mBefore;
    scenefolders::Snapshot mAfter;
    /// QPointer, not a raw one: the command outlives the gesture and the panel
    /// is a child widget — an undo after a teardown must be inert, not a
    /// dangling call.
    QPointer<SceneHierarchyWidget> mPanel;
    bool mFirstRedo = true;
};

#endif   // SCENEFOLDERCOMMAND_H
