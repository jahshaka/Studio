/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/worldmodecommand.h"

#include "irisgl/document/scenegraph/scene.h"
#include "services/worldmodes.h"

WorldModeCommand::Snapshot WorldModeCommand::capture(const iris::ScenePtr &scene)
{
    Snapshot snap;
    if (!scene) return snap;
    snap.worldMode = scene->worldMode;
    snap.photonTier = scene->giTier;
    snap.overrides = scene->worldOverrides;
    snap.cascadeSet = scene->giCascadeSet;
    snap.cascadeInstanceCap = scene->giCascadeInstanceCap;
    for (const worldmodes::Row &r : worldmodes::rows())
        if (r.get) snap.rowValues.insert(r.id, r.get(scene));
    return snap;
}

bool WorldModeCommand::same(const Snapshot &a, const Snapshot &b)
{
    return a.worldMode == b.worldMode && a.photonTier == b.photonTier &&
           a.overrides == b.overrides && a.rowValues == b.rowValues &&
           a.cascadeSet == b.cascadeSet && a.cascadeInstanceCap == b.cascadeInstanceCap;
}

WorldModeCommand::WorldModeCommand(const QString &text, const iris::ScenePtr &scene,
                                   const Snapshot &before, QUndoCommand *parent)
    : StudioCommand(parent), mScene(scene), mBefore(before)
{
    setText(text);
    mAfter = capture(scene);
}

void WorldModeCommand::apply(const Snapshot &snap)
{
    auto scene = mScene.lock();
    if (!scene) return;
    // Fields first, through the registry's own setters — they are the only
    // things that know how a row reaches its backing field.
    for (const worldmodes::Row &r : worldmodes::rows()) {
        if (!r.set) continue;
        const auto it = snap.rowValues.constFind(r.id);
        if (it != snap.rowValues.constEnd()) r.set(scene, it.value());
    }
    // Then the bookkeeping, verbatim. Restoring the pin map through
    // setRowValue() would be wrong twice over: it re-records pins for rows that
    // had none, and a row with no backing field pins itself unconditionally.
    scene->worldOverrides = snap.overrides;
    scene->worldMode = snap.worldMode;
    scene->giCascadeSet = snap.cascadeSet;
    scene->giCascadeInstanceCap = snap.cascadeInstanceCap;
    // The Photon tier last: the `photon` row's setter writes it too, but only
    // when the row is ON — an undo back into "Photon off" would otherwise lose
    // which quality the scene comes back at.
    scene->giTier = snap.photonTier;
    if (mRefresh) mRefresh();
}

void WorldModeCommand::undo()
{
    apply(mBefore);
}

void WorldModeCommand::redo()
{
    // push() replays redo() immediately on an edit the caller already applied.
    if (mFirstRedo) { mFirstRedo = false; return; }
    apply(mAfter);
}
