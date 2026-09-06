/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ANIMATIONEDITS_H
#define ANIMATIONEDITS_H

// Writing keyframes onto the document, ONCE (SCRIPTING_SPEC §2.3, API-first).
//
// WHY THIS IS A SERVICE AND NOT TWO COPIES OF A SWITCH STATEMENT: keying a
// property is four decisions, not one. Which reflected property is this, and
// can it even be animated (only Float/Vec3/Color have a PropertyAnim shape)?
// Does a track for it exist on this animation, and if it does, is it the shape
// this property now needs? Is there already a key at this time — because two
// keys at one time is not "two keys", it is an interpolation with a zero
// denominator? And what does the animation's LENGTH become afterwards?
//
// The Timeline panel's insert-key button and the anim.* verbs both need
// exactly those four answers, and the second copy would be the one that
// forgets the third. So the panel and the verbs go through here, which is the
// same shape planarreflectors:: has for the reflector flag.
//
// Everything in here is pure document work: no Qt widgets, no engine, no
// database — which is what makes the verbs headless-testable.

#include "irisgl/core/math/vec.h"
#include <QList>
#include <QString>
#include <QVariant>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/properties/property.h"

namespace iris {
class PropertyAnim;
class FloatKeyFrame;
}

namespace animedits {

/// Two keys closer than this in time ARE the same key: writing at 1.0 when a
/// key already sits at 1.0 overwrites it rather than stacking a second one.
/// Seconds; well below one frame at any sane rate.
constexpr double kKeyEpsilon = 1e-4;

/// true for the property types a keyframe track can be built for.
///
/// The document nodes reflect bool/int/string/texture/vec2/vec4 fields too
/// (name, visible, lightType, meshPath...) and there is no PropertyAnim
/// subclass for any of them. Before this predicate existed, the unhandled
/// types fell through a `Q_ASSERT(false)` default onto an *uninitialised*
/// PropertyAnim* — a no-op assert plus an indeterminate dereference in
/// release builds.
bool isAnimatablePropertyType(iris::PropertyType type);

/// Builds the PropertyAnim for `type`, named `name`. Returns nullptr — never
/// an indeterminate pointer — for every type isAnimatablePropertyType()
/// rejects. The caller owns the result until it is handed to
/// Animation::addPropertyAnim.
iris::PropertyAnim *makePropertyAnim(iris::PropertyType type, const QString &name);

/// One reflected property of a node, resolved: what it is called, what shape
/// it has and what it currently holds. `index` is its position in the node's
/// own getProperties() list (the Timeline menu keys off it).
struct PropertyInfo
{
    QString name;
    QString displayName;
    iris::PropertyType type = iris::PropertyType::None;
    QVariant value;
    int index = -1;

    bool isValid() const { return index >= 0; }
};

/// Every property of `node` a track can be built for, in the node's own order.
QList<PropertyInfo> animatableProperties(const iris::SceneNodePtr &node);

/// One property by name. `isValid()` is false when the node has no such
/// property OR when it has one that cannot be animated — the caller reports
/// the difference from animatableProperties() if it cares.
PropertyInfo animatableProperty(const iris::SceneNodePtr &node, const QString &name);

/// The animation's track for `prop`, creating it when absent. Returns nullptr
/// (with `error` filled) when the property cannot be animated, or when a track
/// of that name already exists in a DIFFERENT shape than the property now
/// needs — a Float track for what is now a Vec3, say, after a document change.
iris::PropertyAnim *trackFor(const iris::AnimationPtr &anim, const PropertyInfo &prop,
                             bool *created = nullptr, QString *error = nullptr);

/// Writes `value` at `time` on `anim`'s track for `prop`, creating the track
/// if needed and OVERWRITING any key already at that time. An invalid
/// `value` means "key what the node holds right now", which is what the
/// Timeline's insert button does. Recomputes the animation length.
bool setKeyframe(const iris::AnimationPtr &anim, const PropertyInfo &prop,
                 double time, const QVariant &value = QVariant(),
                 bool *createdTrack = nullptr, QString *error = nullptr);

/// Removes every key at `time` (within kKeyEpsilon) from every channel of the
/// property's track. Returns how many keys went. Recomputes the lengths.
int removeKeyframe(const iris::AnimationPtr &anim, const QString &property, double time);

/// Deletes the whole track for `property`. False when there was none.
bool removeTrack(const iris::AnimationPtr &anim, const QString &property);

/// Removes `anim` from `node` — and clears the node's ACTIVE animation when
/// that is the one that went. SceneNode::deleteAnimation only drops it from
/// the list: the node otherwise goes on holding (and posing from, and keying
/// into) a clip that no longer appears anywhere.
bool removeAnimation(const iris::SceneNodePtr &node, const iris::AnimationPtr &anim);

/// The track's value at `time`, as the property's own type (float, iris::Vec3
/// or QColor). Invalid QVariant when there is no such track. Pure read — it
/// does NOT pose the node.
QVariant sampleTrack(const iris::AnimationPtr &anim, const QString &property, double time);

// ---- undo (F16) -----------------------------------------------------------
//
// EVERY anim.* write is a TRACK-level edit: setting a key, removing a key,
// re-shaping a key's tangents and deleting a whole property all begin and end
// with "what did this property's track look like". So undo needs exactly one
// primitive — snapshot the track, restore the track — rather than four bespoke
// inverse operations, each of which would be the one that forgets that
// removing the last key of a channel also moves the animation's LENGTH.
//
// The snapshot is a plain value: it owns no document object and stays valid
// after the track it came from is deleted, which is what lets an undo command
// hold one for the whole life of the stack. A snapshot with `existed == false`
// is the honest "there was no track here", and restoring it removes whatever
// track is there now — that is how the undo of "the first key on a property"
// takes the track away again instead of leaving an empty one behind.

/// One key of one channel, with its full curve shape.
struct KeySnapshot
{
    double time = 0.0;
    float  value = 0.0f;
    int    leftTangent = 0;      // iris::TangentType
    int    rightTangent = 0;
    float  leftSlope = 0.0f;
    float  rightSlope = 0.0f;
    int    handleMode = 0;       // iris::HandleMode
};

/// One float channel of a track ("X", "R", or the track's own name for a
/// single-channel float track).
struct ChannelSnapshot
{
    QString name;
    QList<KeySnapshot> keys;
};

/// A property's whole keyframe track, as data.
struct TrackSnapshot
{
    bool exists = false;         ///< false = there was no track for this property
    QString property;
    QList<ChannelSnapshot> channels;
};

/// Copies `property`'s track out of `anim`. `exists` is false when there is
/// none — which is a snapshot worth taking, not a failure.
TrackSnapshot snapshotTrack(const iris::AnimationPtr &anim, const QString &property);

/// Puts `snap` back: removes whatever track the property has now and rebuilds
/// it from the snapshot (nothing rebuilt when `exists` is false). Recomputes
/// the animation length. Idempotent — the property that makes it safe as the
/// body of both undo() and redo(), given QUndoStack::push replays redo().
bool restoreTrack(const iris::AnimationPtr &anim, const TrackSnapshot &snap);

}   // namespace animedits

#endif   // ANIMATIONEDITS_H
