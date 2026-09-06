/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ANIMATIONCOMMANDS_H
#define ANIMATIONCOMMANDS_H

// Undo for the keyframe domain (verb-coverage audit F16).
//
// Every anim.* write used to say "Not undoable" in its own doc string, and the
// Timeline panel's insert-key / delete-property / delete-animation buttons had
// no undo either — the one panel in the editor where a misclick was permanent.
// Both callers go through the SAME animedits:: service already, so undo is a
// property of that service's edits rather than of either caller: the commands
// here wrap animedits calls, and the panel gets undo by pushing the same
// command the verb pushes.
//
// FOUR of the five are TRACK edits (set a key, remove a key, re-shape a key's
// tangents, delete a property's track), and their inverse is the same
// operation: put the property's track back the way it was. So they share one
// base that holds a before/after animedits::TrackSnapshot pair and does
// nothing but restore one of them. The fifth (removing a whole animation from
// a node) is animation-level and keeps the AnimationPtr alive itself.
//
// CONTRACT, as for NodeEditCommand: the edit is applied and verified by the
// caller FIRST and the command is pushed after, so a refused edit never leaves
// an undo entry — which also means redo() must be idempotent, and restoring a
// snapshot is (it rebuilds the track from data, it does not merge into it).

#include <QString>
#include <QWeakPointer>

#include "commands/studiocommand.h"
#include "irisgl/irisglfwd.h"
#include "services/animationedits.h"

/// Base: restore `before` on undo, `after` on redo. Never used directly — the
/// five named commands below exist so the undo view names the edit.
class AnimTrackCommand : public StudioCommand
{
public:
    AnimTrackCommand(const QString &text, const iris::AnimationPtr &anim,
                     const animedits::TrackSnapshot &before,
                     const animedits::TrackSnapshot &after);

    void undo() override;
    void redo() override;

protected:
    iris::AnimationPtr mAnim;
    animedits::TrackSnapshot mBefore;
    animedits::TrackSnapshot mAfter;
};

/// anim.keyframe / the Timeline's insert-key button.
class SetKeyframeCommand : public AnimTrackCommand
{
public:
    SetKeyframeCommand(const iris::AnimationPtr &anim, const QString &property,
                       const animedits::TrackSnapshot &before,
                       const animedits::TrackSnapshot &after);
};

/// anim.removeKeyframe.
class RemoveKeyframeCommand : public AnimTrackCommand
{
public:
    RemoveKeyframeCommand(const iris::AnimationPtr &anim, const QString &property,
                          const animedits::TrackSnapshot &before,
                          const animedits::TrackSnapshot &after);
};

/// anim.setKeyTangents.
class SetTangentsCommand : public AnimTrackCommand
{
public:
    SetTangentsCommand(const iris::AnimationPtr &anim, const QString &property,
                       const animedits::TrackSnapshot &before,
                       const animedits::TrackSnapshot &after);
};

/// anim.removeProperty / the Timeline's delete-property menu.
class RemovePropertyCommand : public AnimTrackCommand
{
public:
    RemovePropertyCommand(const iris::AnimationPtr &anim, const QString &property,
                          const animedits::TrackSnapshot &before);
};

/// anim.remove / the Timeline's delete-animation button.
///
/// The command KEEPS the AnimationPtr, which is the whole reason undo can put
/// the clip back with its keys intact: SceneNode::deleteAnimation only drops
/// the shared pointer from the node's list. The node is held weakly — an undo
/// stack outlives a deleted node, and re-adding a clip to a corpse is worse
/// than doing nothing.
class RemoveAnimationCommand : public StudioCommand
{
public:
    RemoveAnimationCommand(const iris::SceneNodePtr &node, const iris::AnimationPtr &anim,
                           bool wasActive);

    void undo() override;
    void redo() override;

private:
    QWeakPointer<iris::SceneNode> mNode;
    iris::AnimationPtr mAnim;
    bool mWasActive = false;
};

#endif // ANIMATIONCOMMANDS_H
