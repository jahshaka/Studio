/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/animapi.h"

#include <QColor>
#include <QVector3D>

#include "scripting/modules/moduleshared.h"
#include "services/animationedits.h"
#include "services/sceneeditservice.h"
#include "services/services.h"

#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/propertyanim.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

using namespace scriptmod;

namespace {

/// The serializer's vocabulary for a track shape ("float"/"vector3"/"color"),
/// so a script reading anim.keyframes sees the same words the .jah file uses.
QString trackTypeName(iris::PropertyType type)
{
    switch (type) {
    case iris::PropertyType::Float: return QStringLiteral("float");
    case iris::PropertyType::Vec3:  return QStringLiteral("vector3");
    case iris::PropertyType::Color: return QStringLiteral("color");
    default:                        return QStringLiteral("unsupported");
    }
}

QString trackTypeName(int channels)
{
    switch (channels) {
    case 1:  return QStringLiteral("float");
    case 3:  return QStringLiteral("vector3");
    case 4:  return QStringLiteral("color");
    default: return QStringLiteral("unsupported");
    }
}

/// A property value as JSON: vectors become {x,y,z}, colours "#rrggbb".
QVariant propertyValueToJs(iris::PropertyType type, const QVariant &value)
{
    switch (type) {
    case iris::PropertyType::Vec3:  return vecToJs(value.value<QVector3D>());
    case iris::PropertyType::Color: return colorToJs(value.value<QColor>());
    default:                        return value;
    }
}

}   // namespace

QVector<VerbInfo> AnimApi::verbs() const
{
    return {
        { "list", "anim.list(id) -> [{name, index, active, length, looping, frameRate, properties, skeletal}]",
          "Every animation on the node, in document order. `active` marks the one keyframe writes "
          "and playback use; `properties` lists the property names that have a keyframe track; "
          "`skeletal` is true for a clip that came in with a rigged model (authored by the "
          "importer, played by the engine, not editable here). `length` is derived from the keys.",
          Needs::Document },
        { "create", "anim.create(id, name?) -> name",
          "Adds an animation to the node and makes it ACTIVE — the same thing the Timeline's + "
          "button does. Without a name it is called Animation<n>. Names are not forced unique; "
          "verbs that take a name resolve the first match, so pick your own.",
          Needs::Document },
        { "setActive", "anim.setActive(id, nameOrIndex) -> bool",
          "Chooses which of the node's animations is the active one: the one that poses the node "
          "at anim.seek and that keyframe writes land on. Accepts a name or an index into "
          "anim.list.",
          Needs::Document },
        { "remove", "anim.remove(id, nameOrIndex?) -> bool",
          "Deletes an animation from the node, the active one when no name is given. If the "
          "active one goes, the node is left with no active animation (it keeps whatever pose it "
          "was last posed into) — call anim.setActive to pick another. Not undoable.",
          Needs::Document },
        { "setLooping", "anim.setLooping(id, loop) -> bool",
          "Whether the ACTIVE animation wraps: looping samples time modulo the animation length, "
          "so a 2-second clip poses t=5 as t=1. Off, times past the end hold the last key.",
          Needs::Document },
        { "length", "anim.length(id) -> seconds",
          "The active animation's length: the time of the last key of its longest track. DERIVED, "
          "not settable — every keyframe write recomputes it, so a setter would be a lie one key "
          "later.",
          Needs::Document },
        { "properties", "anim.properties(id) -> [{name, displayName, type, value, animated}]",
          "The node's reflected properties that a keyframe track can be built for — float, vec3 "
          "and colour ones (position, rotation, scale, and on a light intensity, lightColor, "
          "distance, spotCutOff...). This is the Timeline's insert-key menu. `animated` says the "
          "active animation already has a track for it.",
          Needs::Document },
        { "keyframe", "anim.keyframe(id, property, time, value?) -> bool",
          "Sets a keyframe on the ACTIVE animation at `time` seconds, creating the track on first "
          "use. Without a value it keys what the node holds right now (the Timeline's insert-key "
          "button). A key already at that time is OVERWRITTEN, never doubled. Values are whole "
          "property values — a number, {x,y,z} or [x,y,z], \"#rrggbb\" — and the split into the "
          "track's float channels is done here. Not undoable. WHAT ACTUALLY POSES: the document "
          "evaluates position/rotation/scale on any node, a light's intensity, lightColor, "
          "distance, spotCutOff, spotCutOffSoftness, rectWidth and rectHeight, and a decal's "
          "width, height, depth, metalness and roughness. A track on any other property is "
          "authored, saved and readable through anim.sample, but nothing applies it. Note the "
          "light and decal rows are sampled at the RAW time — only position/rotation/scale honour "
          "looping's wrap.",
          Needs::Document },
        { "removeKeyframe", "anim.removeKeyframe(id, property, time) -> bool",
          "Removes the key at `time` from every channel of the property's track on the active "
          "animation. False when there was no key there. Not undoable.",
          Needs::Document },
        { "keyframes", "anim.keyframes(id, property) -> {property, type, length, tracks: [{name, keys: [{time, value}]}]}",
          "Reads a property's keys back, CHANNEL BY CHANNEL — a vec3 track reports three tracks "
          "named X/Y/Z, a colour four named R/G/B/A, because that is how the document stores "
          "them: the channels are independent curves and need not share key times. Empty tracks "
          "list = no track for that property.",
          Needs::Document },
        { "removeProperty", "anim.removeProperty(id, property) -> bool",
          "Deletes a property's whole track from the active animation. False when it had none.",
          Needs::Document },
        { "sample", "anim.sample(id, property, time) -> value",
          "Evaluates a property's track at `time` WITHOUT posing anything — the interpolated "
          "value between the surrounding keys (linear; before the first and after the last key it "
          "holds). Undefined when the property has no track. Note this samples the track raw: it "
          "does not apply looping, which anim.seek does.",
          Needs::Document },
        { "seek", "anim.seek(time?) -> seconds",
          "Moves the scene's animation clock to `time` seconds and poses EVERY node in the scene "
          "from its active animation — the document evaluation the Timeline cursor does, and the "
          "time SceneMirror hands the engine. Looping animations wrap (time modulo length); the "
          "properties that actually pose are the ones anim.keyframe documents. Called with no "
          "argument it reads the clock without moving it. Negative times clamp to zero.",
          Needs::Document },
    };
}

iris::SceneNodePtr AnimApi::nodeOrFail(const QString &id, const QString &verb)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) {
        fail(QStringLiteral("%1: no scene is open").arg(verb));
        return iris::SceneNodePtr();
    }
    auto node = findNodeByGuid(scene->getRootNode(), id);
    if (!node) fail(QStringLiteral("%1: no node with id '%2'").arg(verb, id));
    return node;
}

iris::AnimationPtr AnimApi::activeOrFail(const iris::SceneNodePtr &node, const QString &verb)
{
    auto anim = node->getAnimation();
    if (!anim) {
        fail(QStringLiteral("%1: '%2' has no active animation — call anim.create(id) first")
                 .arg(verb, node->getName()));
        return iris::AnimationPtr();
    }
    return anim;
}

iris::AnimationPtr AnimApi::find(const iris::SceneNodePtr &node, const QString &name)
{
    const auto animations = node->getAnimations();
    for (const auto &anim : animations) {
        if (anim && anim->getName() == name)
            return anim;
    }
    // Not a name anything carries: an index into anim.list is the other thing
    // a caller could reasonably mean (and the writer serializes the active one
    // as an index, so scripts see indices).
    bool isNumber = false;
    const int index = name.toInt(&isNumber);
    if (isNumber && index >= 0 && index < animations.count())
        return animations[index];
    return iris::AnimationPtr();
}

QVariantList AnimApi::list(const QString &id)
{
    QVariantList out;
    auto node = nodeOrFail(id, QStringLiteral("anim.list"));
    if (!node) return out;

    const auto animations = node->getAnimations();
    const auto active = node->getAnimation();
    int index = 0;
    for (const auto &anim : animations) {
        const int animIndex = index++;
        if (!anim) continue;
        QVariantMap m;
        m["name"] = anim->getName();
        m["index"] = animIndex;
        m["active"] = (anim == active);
        m["length"] = anim->getLength();
        m["looping"] = anim->getLooping();
        m["frameRate"] = anim->getFrameRate();
        m["skeletal"] = anim->hasSkeletalAnimation();
        QVariantList props;
        for (auto it = anim->properties.constBegin(); it != anim->properties.constEnd(); ++it)
            props.append(it.key());
        m["properties"] = props;
        out.append(m);
    }
    return out;
}

QString AnimApi::create(const QString &id, const QString &name)
{
    auto node = nodeOrFail(id, QStringLiteral("anim.create"));
    if (!node) return QString();

    const QString animName = name.isEmpty()
            ? QStringLiteral("Animation%1").arg(node->getAnimations().count() + 1)
            : name;
    auto anim = iris::Animation::create(animName);
    node->addAnimation(anim);
    node->setAnimation(anim);
    return animName;
}

bool AnimApi::setActive(const QString &id, const QString &name)
{
    auto node = nodeOrFail(id, QStringLiteral("anim.setActive"));
    if (!node) return false;
    auto anim = find(node, name);
    if (!anim)
        return fail(QStringLiteral("anim.setActive: '%1' has no animation '%2'")
                        .arg(node->getName(), name));
    node->setAnimation(anim);
    return true;
}

bool AnimApi::remove(const QString &id, const QString &name)
{
    auto node = nodeOrFail(id, QStringLiteral("anim.remove"));
    if (!node) return false;

    iris::AnimationPtr anim;
    if (name.isEmpty()) {
        anim = activeOrFail(node, QStringLiteral("anim.remove"));
        if (!anim) return false;
    } else {
        anim = find(node, name);
        if (!anim)
            return fail(QStringLiteral("anim.remove: '%1' has no animation '%2'")
                            .arg(node->getName(), name));
    }

    // Shared with the Timeline's delete button: SceneNode::deleteAnimation
    // only drops the clip from the list, so the active pointer has to be
    // cleared with it (animedits::removeAnimation).
    return animedits::removeAnimation(node, anim);
}

bool AnimApi::setLooping(const QString &id, bool loop)
{
    auto node = nodeOrFail(id, QStringLiteral("anim.setLooping"));
    if (!node) return false;
    auto anim = activeOrFail(node, QStringLiteral("anim.setLooping"));
    if (!anim) return false;
    anim->setLooping(loop);
    return true;
}

double AnimApi::length(const QString &id)
{
    auto node = nodeOrFail(id, QStringLiteral("anim.length"));
    if (!node) return 0.0;
    auto anim = activeOrFail(node, QStringLiteral("anim.length"));
    if (!anim) return 0.0;
    return anim->getLength();
}

QVariantList AnimApi::properties(const QString &id)
{
    QVariantList out;
    auto node = nodeOrFail(id, QStringLiteral("anim.properties"));
    if (!node) return out;

    const auto active = node->getAnimation();
    for (const auto &prop : animedits::animatableProperties(node)) {
        QVariantMap m;
        m["name"] = prop.name;
        m["displayName"] = prop.displayName;
        m["type"] = trackTypeName(prop.type);
        m["value"] = propertyValueToJs(prop.type, prop.value);
        m["animated"] = active ? active->hasPropertyAnim(prop.name) : false;
        out.append(m);
    }
    return out;
}

bool AnimApi::keyframe(const QString &id, const QString &property, double time, const QVariant &value)
{
    auto node = nodeOrFail(id, QStringLiteral("anim.keyframe"));
    if (!node) return false;
    auto anim = activeOrFail(node, QStringLiteral("anim.keyframe"));
    if (!anim) return false;

    const auto prop = animedits::animatableProperty(node, property);
    if (!prop.isValid())
        return fail(QStringLiteral("anim.keyframe: '%1' has no animatable property '%2' "
                                   "(anim.properties lists them)")
                        .arg(node->getName(), property));

    // Whole property values in, channels out: the JSON shapes every other verb
    // speaks ({x,y,z}, "#rrggbb", a number), resolved against what the node
    // holds now so a partial vector nudges one axis.
    QVariant typed;
    const QVariant raw = normalizeJs(value);
    if (raw.isValid() && !raw.isNull()) {
        switch (prop.type) {
        case iris::PropertyType::Vec3:
            typed = vecFromJs(raw, prop.value.value<QVector3D>());
            break;
        case iris::PropertyType::Color:
            typed = colorFromJs(raw, prop.value.value<QColor>());
            break;
        default:
            typed = raw.toFloat();
            break;
        }
    }

    QString error;
    if (!animedits::setKeyframe(anim, prop, qMax(0.0, time), typed, nullptr, &error))
        return fail(QStringLiteral("anim.keyframe: %1").arg(error));
    return true;
}

bool AnimApi::removeKeyframe(const QString &id, const QString &property, double time)
{
    auto node = nodeOrFail(id, QStringLiteral("anim.removeKeyframe"));
    if (!node) return false;
    auto anim = activeOrFail(node, QStringLiteral("anim.removeKeyframe"));
    if (!anim) return false;
    return animedits::removeKeyframe(anim, property, time) > 0;
}

QVariantMap AnimApi::keyframes(const QString &id, const QString &property)
{
    QVariantMap out;
    auto node = nodeOrFail(id, QStringLiteral("anim.keyframes"));
    if (!node) return out;
    auto anim = activeOrFail(node, QStringLiteral("anim.keyframes"));
    if (!anim) return out;

    out["property"] = property;
    out["length"] = anim->getLength();
    QVariantList tracks;

    if (anim->hasPropertyAnim(property)) {
        if (auto *track = anim->getPropertyAnim(property)) {
            const auto frames = track->getKeyFrames();
            out["type"] = trackTypeName(frames.count());
            for (const auto &info : frames) {
                QVariantMap t;
                t["name"] = info.name;
                QVariantList keys;
                if (info.keyFrame) {
                    for (auto *key : info.keyFrame->keys) {
                        QVariantMap k;
                        k["time"] = key->time;
                        k["value"] = key->value;
                        keys.append(k);
                    }
                }
                t["keys"] = keys;
                tracks.append(t);
            }
        }
    }
    if (!out.contains("type")) out["type"] = QStringLiteral("none");
    out["tracks"] = tracks;
    return out;
}

bool AnimApi::removeProperty(const QString &id, const QString &property)
{
    auto node = nodeOrFail(id, QStringLiteral("anim.removeProperty"));
    if (!node) return false;
    auto anim = activeOrFail(node, QStringLiteral("anim.removeProperty"));
    if (!anim) return false;
    return animedits::removeTrack(anim, property);
}

QVariant AnimApi::sample(const QString &id, const QString &property, double time)
{
    auto node = nodeOrFail(id, QStringLiteral("anim.sample"));
    if (!node) return QVariant();
    auto anim = activeOrFail(node, QStringLiteral("anim.sample"));
    if (!anim) return QVariant();

    const QVariant sampled = animedits::sampleTrack(anim, property, time);
    if (!sampled.isValid()) return QVariant();
    if (sampled.typeId() == QMetaType::QVector3D) return vecToJs(sampled.value<QVector3D>());
    if (sampled.typeId() == QMetaType::QColor)    return colorToJs(sampled.value<QColor>());
    return sampled;
}

double AnimApi::seek(const QVariant &time)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) {
        fail(QStringLiteral("anim.seek: no scene is open"));
        return 0.0;
    }
    const QVariant raw = normalizeJs(time);
    if (raw.isValid() && !raw.isNull()) {
        bool ok = false;
        const double seconds = raw.toDouble(&ok);
        if (!ok) {
            fail(QStringLiteral("anim.seek: '%1' is not a time in seconds").arg(raw.toString()));
            return scene->animationTime();
        }
        scene->updateSceneAnimation(float(qMax(0.0, seconds)));
    }
    return scene->animationTime();
}
