/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef WORLDMODECOMMAND_H
#define WORLDMODECOMMAND_H

// One undo step for one World Mode gesture (POST_CHAIN_SPEC §9 + the World-Mode
// refresh defect, 2026-09-06).
//
// Picking a tier is the single widest edit the World panel can make: setMode()
// writes THIRTEEN backing fields at once (MSAA, HDR, bloom, SSAO, SMAA, SSR,
// refractions, shadow resolution and filter, GI mode and quality, sky bake
// resolution, ambient-from-sky) plus the tier itself, skipping only the rows the
// user pinned. Before this, none of it was undoable — going Epic -> High to see
// what it looked like was a one-way door.
//
// Same shape and same reasoning as SceneFolderCommand: the state is small and
// the operation is wide, so the command SNAPSHOTS it (tier + pins + every row's
// resolved value) rather than inventing an inverse for each row. Restoring
// writes the fields through the registry's own setters, then puts the pin map
// and the tier back verbatim — which keeps the system's one invariant true, "a
// backing field is ALWAYS the resolved value".

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <functional>

#include "commands/studiocommand.h"
#include "irisgl/irisglfwd.h"

class WorldModeCommand : public StudioCommand
{
public:
    struct Snapshot
    {
        int worldMode = -1;
        QJsonObject overrides;
        QHash<QString, int> rowValues;   ///< rowId -> backing-field value
    };

    static Snapshot capture(const iris::ScenePtr &scene);

    /// `before` must be captured BEFORE the edit; the "after" state is captured
    /// here, so construct this after applying and push it after that.
    WorldModeCommand(const QString &text, const iris::ScenePtr &scene, const Snapshot &before);

    void undo() override;
    void redo() override;

    /// Called after every undo/redo so the panel that owns the rows re-reads
    /// them. Optional (null in scripts and tests).
    void setRefresh(std::function<void()> refresh) { mRefresh = std::move(refresh); }

private:
    void apply(const Snapshot &snap);

    iris::SceneWPtr mScene;
    Snapshot mBefore;
    Snapshot mAfter;
    std::function<void()> mRefresh;
    bool mFirstRedo = true;
};

#endif   // WORLDMODECOMMAND_H
