/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef NODEEDITCOMMAND_H
#define NODEEDITCOMMAND_H

// NodeEditCommand — an undo entry for a node edit whose apply half already
// exists as a SERVICE call (AI_SURFACE_AUDIT F5).
//
// The asset-binding verbs (node.setLightProfile / setLightTexture /
// setDecalTexture / setParticleTexture) and node.setPlanarReflector were all
// "direct document write — not undoable yet". Each of them is already a single
// call into the one shared implementation (LightBindings, SceneEditService,
// planarreflectors::), so a per-verb command class would be five copies of the
// same two lines. This carries the two calls instead.
//
// Contract for callers, and the reason it is safe:
//   * the edit is applied and VERIFIED first; the command is pushed after, so
//     a refused edit never leaves an undo entry;
//   * QUndoStack::push() replays redo() immediately, so `redoFn` must be
//     idempotent — every binding here re-resolves the same guid and re-pins an
//     already-pinned dependency, which is a no-op;
//   * captures must own what they touch (SceneNodePtr by value). Database and
//     Project outlive the undo stack, which is cleared on project close.
//
// It does NOT unpin the project dependency on undo. An orphaned pin is inert
// (assets.gc reclaims it); un-pinning would delete a row another node may share.

#include <functional>

#include <QString>

#include "commands/studiocommand.h"

class NodeEditCommand : public StudioCommand
{
public:
    NodeEditCommand(const QString &text, std::function<void()> redoFn,
                    std::function<void()> undoFn);

    void undo() override;
    void redo() override;

private:
    std::function<void()> mRedo;
    std::function<void()> mUndo;
};

#endif // NODEEDITCOMMAND_H
