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
    snap.overrides = scene->worldOverrides;
    for (const worldmodes::Row &r : worldmodes::rows())
        if (r.get) snap.rowValues.insert(r.id, r.get(scene));
    return snap;
}

WorldModeCommand::WorldModeCommand(const QString &text, const iris::ScenePtr &scene,
                                   const Snapshot &before)
    : mScene(scene), mBefore(before)
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
