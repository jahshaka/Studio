/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENELOOKSCOMMAND_H
#define SCENELOOKSCOMMAND_H

// One undo step for one edit of the scene's LOOKS STACK
// (SPECS/POST_LOOKS_SPEC.md §4.1).
//
// SNAPSHOT, not an inverse, for the same reason WorldModeCommand is one: the
// state is a small JSON array and the operations are wide (a reorder moves
// every entry after the moved one; a remove renumbers the rest). Storing the
// array twice is a few hundred bytes and cannot be wrong; five inverses could.
//
// Every looks verb pushes one of these, so a script's `world.addLook` is one
// step in the editor's undo stack exactly as the panel's Add button is.

#include <QJsonArray>
#include <QString>

#include <functional>

#include "commands/studiocommand.h"
#include "irisgl/irisglfwd.h"

class SceneLooksCommand : public StudioCommand
{
public:
    /// `before` must be captured BEFORE the edit; the state as it stands now is
    /// captured here, so construct and push this after applying.
    SceneLooksCommand(const QString &text, const iris::ScenePtr &scene,
                      const QJsonArray &before);

    void undo() override;
    void redo() override;

    /// Called after every undo/redo so a panel showing the stack re-reads it.
    /// Optional (null in scripts and tests).
    void setRefresh(std::function<void()> refresh) { mRefresh = std::move(refresh); }

private:
    void apply(const QJsonArray &stack);

    iris::SceneWPtr mScene;
    QJsonArray mBefore;
    QJsonArray mAfter;
    std::function<void()> mRefresh;
    bool mFirstRedo = true;
};

#endif   // SCENELOOKSCOMMAND_H
