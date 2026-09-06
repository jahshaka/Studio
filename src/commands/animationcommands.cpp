/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/animationcommands.h"

#include <QObject>

#include "irisgl/document/animation/animation.h"
#include "irisgl/document/scenegraph/scenenode.h"

AnimTrackCommand::AnimTrackCommand(const QString &text, const iris::AnimationPtr &anim,
                                   const animedits::TrackSnapshot &before,
                                   const animedits::TrackSnapshot &after)
    : StudioCommand(text), mAnim(anim), mBefore(before), mAfter(after)
{
}

void AnimTrackCommand::undo()
{
    animedits::restoreTrack(mAnim, mBefore);
}

void AnimTrackCommand::redo()
{
    // Idempotent by construction: the caller already applied the edit and
    // `after` IS the result of it, so QUndoStack::push's immediate replay is a
    // rebuild of the same track from the same data.
    animedits::restoreTrack(mAnim, mAfter);
}

SetKeyframeCommand::SetKeyframeCommand(const iris::AnimationPtr &anim, const QString &property,
                                       const animedits::TrackSnapshot &before,
                                       const animedits::TrackSnapshot &after)
    : AnimTrackCommand(QObject::tr("Set keyframe (%1)").arg(property), anim, before, after)
{
}

RemoveKeyframeCommand::RemoveKeyframeCommand(const iris::AnimationPtr &anim, const QString &property,
                                             const animedits::TrackSnapshot &before,
                                             const animedits::TrackSnapshot &after)
    : AnimTrackCommand(QObject::tr("Remove keyframe (%1)").arg(property), anim, before, after)
{
}

SetTangentsCommand::SetTangentsCommand(const iris::AnimationPtr &anim, const QString &property,
                                       const animedits::TrackSnapshot &before,
                                       const animedits::TrackSnapshot &after)
    : AnimTrackCommand(QObject::tr("Set key tangents (%1)").arg(property), anim, before, after)
{
}

RemovePropertyCommand::RemovePropertyCommand(const iris::AnimationPtr &anim, const QString &property,
                                             const animedits::TrackSnapshot &before)
    : AnimTrackCommand(QObject::tr("Remove animated property (%1)").arg(property), anim, before,
                       animedits::TrackSnapshot{ false, property, {} })
{
}

RemoveAnimationCommand::RemoveAnimationCommand(const iris::SceneNodePtr &node,
                                               const iris::AnimationPtr &anim, bool wasActive)
    : StudioCommand(QObject::tr("Remove animation (%1)").arg(anim ? anim->getName() : QString())),
      mNode(node.toWeakRef()), mAnim(anim), mWasActive(wasActive)
{
}

void RemoveAnimationCommand::undo()
{
    auto node = mNode.toStrongRef();
    if (!node || !mAnim) return;
    if (!node->getAnimations().contains(mAnim)) node->addAnimation(mAnim);
    if (mWasActive) node->setAnimation(mAnim);
}

void RemoveAnimationCommand::redo()
{
    auto node = mNode.toStrongRef();
    if (!node || !mAnim) return;
    // animedits::removeAnimation is a no-op when the clip is already gone,
    // which is what makes push()'s immediate replay harmless.
    animedits::removeAnimation(node, mAnim);
}
