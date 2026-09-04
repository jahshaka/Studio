/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/animationedits.h"

#include <QColor>
#include <QVector3D>
#include <algorithm>
#include <cmath>

#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/propertyanim.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace {

/// How many float channels a track of this type has — and, read the other way,
/// what a track's channel count says its type is. PropertyAnim has no type tag
/// of its own; getKeyFrames().count() is the only shape it reports.
int channelCount(iris::PropertyType type)
{
    switch (type) {
    case iris::PropertyType::Float: return 1;
    case iris::PropertyType::Vec3:  return 3;
    case iris::PropertyType::Color: return 4;
    default:                        return 0;
    }
}

/// Writes one channel: overwrites the key already at `time`, else adds one.
/// Returns true when a NEW key was created.
bool putKey(iris::FloatKeyFrame *frame, double time, float value)
{
    for (auto *key : frame->keys) {
        if (std::abs(key->time - time) <= animedits::kKeyEpsilon) {
            key->value = value;
            return false;
        }
    }
    frame->addKey(value, time);
    return true;
}

/// KeyFrame::length is only ever maintained by addKey, so removing keys leaves
/// it reading the time of a key that is gone (and Animation::
/// calculateAnimationLength believes it). Put it back to the honest answer.
void refreshFrameLength(iris::FloatKeyFrame *frame)
{
    frame->length = frame->keys.isEmpty() ? 0.0f : float(frame->keys.last()->time);
}

}   // namespace

namespace animedits {

bool isAnimatablePropertyType(iris::PropertyType type)
{
    return channelCount(type) > 0;
}

iris::PropertyAnim *makePropertyAnim(iris::PropertyType type, const QString &name)
{
    iris::PropertyAnim *anim = nullptr;

    switch (type) {
    case iris::PropertyType::Float: anim = new iris::FloatPropertyAnim();    break;
    case iris::PropertyType::Vec3:  anim = new iris::Vector3DPropertyAnim(); break;
    case iris::PropertyType::Color: anim = new iris::ColorPropertyAnim();    break;
    default:
        // Unsupported property type: no track exists for it.
        return nullptr;
    }

    anim->setName(name);
    return anim;
}

QList<PropertyInfo> animatableProperties(const iris::SceneNodePtr &node)
{
    QList<PropertyInfo> out;
    if (!node) return out;

    // getProperties() hands back freshly allocated Property objects every call
    // and the caller owns them.
    const auto props = node->getProperties();
    int index = 0;
    for (auto *prop : props) {
        const int propIndex = index++;
        if (!prop || !isAnimatablePropertyType(prop->type))
            continue;
        PropertyInfo info;
        info.name = prop->name;
        info.displayName = prop->displayName;
        info.type = prop->type;
        info.value = prop->getValue();
        info.index = propIndex;
        out.append(info);
    }
    qDeleteAll(props);
    return out;
}

PropertyInfo animatableProperty(const iris::SceneNodePtr &node, const QString &name)
{
    for (const auto &info : animatableProperties(node)) {
        if (info.name == name)
            return info;
    }
    return PropertyInfo();
}

iris::PropertyAnim *trackFor(const iris::AnimationPtr &anim, const PropertyInfo &prop,
                             bool *created, QString *error)
{
    if (created) *created = false;
    const auto setError = [error](const QString &message) -> iris::PropertyAnim * {
        if (error) *error = message;
        return nullptr;
    };

    if (!anim)
        return setError(QStringLiteral("no animation"));
    if (!prop.isValid() || !isAnimatablePropertyType(prop.type))
        return setError(QStringLiteral("property '%1' has no keyframe track type").arg(prop.name));

    const int channels = channelCount(prop.type);

    if (anim->hasPropertyAnim(prop.name)) {
        auto *existing = anim->getPropertyAnim(prop.name);
        if (!existing)
            return setError(QStringLiteral("property '%1' has an empty track").arg(prop.name));
        // A track built for a different shape (a Float track for what the
        // document now reports as a Vec3) would be indexed out of range below.
        const auto frames = existing->getKeyFrames();
        if (frames.count() != channels)
            return setError(QStringLiteral("property '%1' has a %2-channel track but needs %3")
                                .arg(prop.name).arg(frames.count()).arg(channels));
        for (const auto &frame : frames) {
            if (!frame.keyFrame)
                return setError(QStringLiteral("property '%1' has an incomplete track").arg(prop.name));
        }
        return existing;
    }

    auto *fresh = makePropertyAnim(prop.type, prop.name);
    if (!fresh)
        return setError(QStringLiteral("property '%1' has no keyframe track type").arg(prop.name));
    anim->addPropertyAnim(fresh);
    if (created) *created = true;
    return fresh;
}

bool setKeyframe(const iris::AnimationPtr &anim, const PropertyInfo &prop,
                 double time, const QVariant &value, bool *createdTrack, QString *error)
{
    auto *track = trackFor(anim, prop, createdTrack, error);
    if (!track) return false;

    // No value given = key what the node holds right now (the Timeline's
    // insert-key button, and the natural default for a script that has just
    // moved something).
    const QVariant val = value.isValid() ? value : prop.value;
    const auto frames = track->getKeyFrames();

    switch (prop.type) {
    case iris::PropertyType::Float:
        putKey(frames[0].keyFrame, time, val.toFloat());
        break;
    case iris::PropertyType::Vec3: {
        const QVector3D v = val.value<QVector3D>();
        putKey(frames[0].keyFrame, time, v.x());
        putKey(frames[1].keyFrame, time, v.y());
        putKey(frames[2].keyFrame, time, v.z());
        break;
    }
    case iris::PropertyType::Color: {
        const QColor c = val.value<QColor>();
        putKey(frames[0].keyFrame, time, c.redF());
        putKey(frames[1].keyFrame, time, c.greenF());
        putKey(frames[2].keyFrame, time, c.blueF());
        putKey(frames[3].keyFrame, time, c.alphaF());
        break;
    }
    default:
        if (error) *error = QStringLiteral("property '%1' has no keyframe track type").arg(prop.name);
        return false;
    }

    anim->calculateAnimationLength();
    return true;
}

int removeKeyframe(const iris::AnimationPtr &anim, const QString &property, double time)
{
    if (!anim || !anim->hasPropertyAnim(property))
        return 0;
    auto *track = anim->getPropertyAnim(property);
    if (!track) return 0;

    int removed = 0;
    for (const auto &info : track->getKeyFrames()) {
        auto *frame = info.keyFrame;
        if (!frame) continue;
        // Collect first: removeKey() mutates the vector being walked. And it
        // only unlinks — the Key is ours to delete or it leaks (the KeyFrame
        // destructor only frees what is still in the list).
        QVector<iris::Key<float> *> doomed;
        for (auto *key : frame->keys) {
            if (std::abs(key->time - time) <= kKeyEpsilon)
                doomed.append(key);
        }
        for (auto *key : doomed) {
            frame->removeKey(key);
            delete key;
            ++removed;
        }
        if (!doomed.isEmpty())
            refreshFrameLength(frame);
    }

    if (removed > 0)
        anim->calculateAnimationLength();
    return removed;
}

bool removeTrack(const iris::AnimationPtr &anim, const QString &property)
{
    if (!anim || !anim->hasPropertyAnim(property))
        return false;
    anim->removePropertyAnim(property);
    anim->calculateAnimationLength();
    return true;
}

bool removeAnimation(const iris::SceneNodePtr &node, const iris::AnimationPtr &anim)
{
    if (!node || !anim) return false;
    const bool wasActive = (node->getAnimation() == anim);
    node->deleteAnimation(anim);
    if (wasActive)
        node->setAnimation(iris::AnimationPtr());
    return true;
}

QVariant sampleTrack(const iris::AnimationPtr &anim, const QString &property, double time)
{
    if (!anim || !anim->hasPropertyAnim(property))
        return QVariant();
    auto *track = anim->getPropertyAnim(property);
    if (!track) return QVariant();

    const auto frames = track->getKeyFrames();
    for (const auto &frame : frames) {
        if (!frame.keyFrame)
            return QVariant();
    }

    // The channel count IS the type (PropertyAnim carries no tag), and reading
    // the channels directly keeps this free of downcasts.
    switch (frames.count()) {
    case 1:
        return frames[0].keyFrame->getValueAt(time);
    case 3:
        return QVector3D(frames[0].keyFrame->getValueAt(time),
                         frames[1].keyFrame->getValueAt(time),
                         frames[2].keyFrame->getValueAt(time));
    case 4: {
        // 0..1 channels, clamped: an extrapolating curve can leave the range,
        // and QColor::fromRgb with an out-of-range int is an INVALID colour
        // rather than a saturated one.
        const auto chan = [&](int i) {
            return std::clamp<float>(frames[i].keyFrame->getValueAt(time), 0.0f, 1.0f);
        };
        return QColor::fromRgbF(chan(0), chan(1), chan(2), chan(3));
    }
    default:
        return QVariant();
    }
}

}   // namespace animedits
