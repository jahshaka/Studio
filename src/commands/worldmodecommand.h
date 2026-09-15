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
#include <QVector>
#include <QJsonObject>
#include <QString>

#include <functional>

#include "commands/studiocommand.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"

class WorldModeCommand : public StudioCommand
{
public:
    struct Snapshot
    {
        int worldMode = -1;
        /// PHOTON's remembered quality tier (GI_UNIFIED_SPEC §2). It is NOT
        /// derivable from the rows: while Photon is off the `photon` row reads
        /// "off" and carries no tier, so an undo of "turn Photon off" would come
        /// back at whatever tier the scene happened to hold.
        int photonTier = 3;
        QJsonObject overrides;
        QHash<QString, int> rowValues;   ///< rowId -> backing-field value
        /// PHOTON'S CASCADE FIELDS THAT ARE NOT ROWS (audit D12, PHOTON_SPEC
        /// §7 E2 (7)). `giCascades` IS a registry row since the tier table
        /// adopted it, so `rowValues` carries it; the table a scene may pin and
        /// the per-cascade instance budget are not rows and would otherwise
        /// survive an undo of the edit that set them — which is the one thing
        /// this command exists to prevent ("going Epic -> High to see what it
        /// looked like was a one-way door").
        QVector<iris::Vec3> cascadeSet;
        int cascadeInstanceCap = 0;
    };

    static Snapshot capture(const iris::ScenePtr &scene);

    /// `before` must be captured BEFORE the edit; the "after" state is captured
    /// here, so construct this after applying and push it after that.
    /// `parent`: a composite step owns it as a child (see ScenePropertyCommand).
    WorldModeCommand(const QString &text, const iris::ScenePtr &scene, const Snapshot &before,
                     QUndoCommand *parent = nullptr);

    /// True when the two snapshots describe the same registry state (tier,
    /// Photon tier, pins and every row's value) — "did this edit change it".
    static bool same(const Snapshot &a, const Snapshot &b);

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
