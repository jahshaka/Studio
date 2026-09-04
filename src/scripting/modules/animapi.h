/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_ANIMAPI_H
#define SCRIPTING_ANIMAPI_H

// anim.* — property animation: the clips on a node, their keyframes, and the
// document clock that evaluates them (SCRIPTING_SPEC §2.3).
//
// This namespace is the KEYFRAME half of animation, which is the half the
// document owns. Skeletal clips are the engine's since the clip evaluator was
// retired (ANIMATION_ENGINE_MIGRATION_SPEC): a rigged model's clips are
// reported here (anim.list says which animations carry one) but are not
// authored here, and avatar.* drives their playback.
//
// The verbs are shaped from the DOCUMENT's model, not from the Timeline
// panel's widgets:
//
//   * a node owns a LIST of animations and has at most one ACTIVE one
//     (SceneNode::animations / animation) — hence list/create/setActive/remove;
//   * an animation owns a TRACK per reflected property, and a track owns one
//     float channel per component (Vector3DPropertyAnim is three FloatKeyFrames
//     called X/Y/Z, not one Vec3 curve) — hence keyframe/keyframes/sample,
//     which speak in whole property values and do the channel split;
//   * LENGTH is derived, never set: Animation::calculateAnimationLength takes
//     it from the last key of the longest track after every write, so a setter
//     would be a lie one keyframe later. anim.length reads it;
//   * TIME is the scene's, not the node's (Scene::animationTime, which is what
//     SceneMirror pushes at the engine) — hence anim.seek, one clock.
//
// Writes are direct document writes, matching the Timeline panel: keyframe
// edits are NOT undoable individually (the panel's insert-key button is not
// either). A script run is still one undo macro for everything undoable it
// does. The panel is not live-refreshed by script writes — reselect the node
// in the hierarchy to see them.

#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"
#include "irisgl/irisglfwd.h"

class AnimApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("anim"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariantList list(const QString &id);
    Q_INVOKABLE QString create(const QString &id, const QString &name = QString());
    Q_INVOKABLE bool setActive(const QString &id, const QString &name);
    Q_INVOKABLE bool remove(const QString &id, const QString &name = QString());
    Q_INVOKABLE bool setLooping(const QString &id, bool loop);
    Q_INVOKABLE double length(const QString &id);
    Q_INVOKABLE QVariantList properties(const QString &id);
    Q_INVOKABLE bool keyframe(const QString &id, const QString &property, double time,
                              const QVariant &value = QVariant());
    Q_INVOKABLE bool removeKeyframe(const QString &id, const QString &property, double time);
    Q_INVOKABLE QVariantMap keyframes(const QString &id, const QString &property);
    Q_INVOKABLE bool setKeyTangents(const QString &id, const QString &property, double time,
                                    const QVariantMap &shape = QVariantMap());
    Q_INVOKABLE bool removeProperty(const QString &id, const QString &property);
    Q_INVOKABLE QVariant sample(const QString &id, const QString &property, double time);
    Q_INVOKABLE double seek(const QVariant &time = QVariant());

private:
    /// The node with this guid, or null with a JS error already thrown.
    iris::SceneNodePtr nodeOrFail(const QString &id, const QString &verb);
    /// The node's ACTIVE animation, or null with a JS error already thrown
    /// (the message names anim.create, which is what the caller forgot).
    iris::AnimationPtr activeOrFail(const iris::SceneNodePtr &node, const QString &verb);
    /// The node's animation by name — or by index, when the string is a number
    /// and no animation carries that name. Null (no error thrown) on a miss.
    iris::AnimationPtr find(const iris::SceneNodePtr &node, const QString &name);
};

#endif // SCRIPTING_ANIMAPI_H
