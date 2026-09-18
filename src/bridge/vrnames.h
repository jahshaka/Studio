/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VRNAMES_H
#define VRNAMES_H

// The VR enums AS WORDS — the spelling every verb, every tooltip and every test
// assertion uses (SPECS/VR_SPEC.md §4.6).
//
// It is one header because there are now two readers of the same runtime state:
// `vr.state()` (the session's own verbs, phase 2) and `player.state().vr` (the
// Player's VR mode, phase 3). A second switch would be a second vocabulary, and
// a test that asserts `state === "focused"` against one of them would silently
// mean something else against the other.

#include <QString>
#include <QVariantMap>

#include "jahshaka/engine/Engine.h"
#include "services/vrorigin.h"

namespace vrnames {

inline QString state(jahshaka::engine::VrState s)
{
    using jahshaka::engine::VrState;
    switch (s) {
    case VrState::Unavailable:  return QStringLiteral("unavailable");
    case VrState::Idle:         return QStringLiteral("idle");
    case VrState::Ready:        return QStringLiteral("ready");
    case VrState::Synchronized: return QStringLiteral("synchronized");
    case VrState::Visible:      return QStringLiteral("visible");
    case VrState::Focused:      return QStringLiteral("focused");
    case VrState::Stopping:     return QStringLiteral("stopping");
    case VrState::Lost:         return QStringLiteral("lost");
    }
    return QStringLiteral("unknown");
}

inline QString mirror(jahshaka::engine::VrMirrorMode m)
{
    using jahshaka::engine::VrMirrorMode;
    switch (m) {
    case VrMirrorMode::None:  return QStringLiteral("none");
    case VrMirrorMode::Left:  return QStringLiteral("left");
    case VrMirrorMode::Right: return QStringLiteral("right");
    case VrMirrorMode::Both:  return QStringLiteral("both");
    }
    return QStringLiteral("left");
}

inline jahshaka::engine::VrMirrorMode mirrorFrom(const QString &name,
                                                 jahshaka::engine::VrMirrorMode fallback)
{
    using jahshaka::engine::VrMirrorMode;
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("none"))  return VrMirrorMode::None;
    if (n == QLatin1String("left"))  return VrMirrorMode::Left;
    if (n == QLatin1String("right")) return VrMirrorMode::Right;
    if (n == QLatin1String("both"))  return VrMirrorMode::Both;
    return fallback;
}

/// A LOCATED POSE AS A VERB ANSWERS IT (phase 4): world position, world
/// rotation, the level HEADING in degrees (the number a script actually
/// reasons with) and whether the runtime located it at all.
///
/// One spelling, for the same reason the two enums above have one: the head is
/// reported by `vr.state().head` and by `player.state().vr.head`, and the hands
/// by `vr.state().hands`, and a caller that learned the shape from one of them
/// must not find a different shape in the next.
inline QVariantMap pose(const jahshaka::engine::Vec3 &position,
                        const jahshaka::engine::Quat &rotation, bool valid)
{
    const iris::Quat rot(rotation.w, rotation.x, rotation.y, rotation.z);
    QVariantMap out;
    out[QStringLiteral("valid")] = valid;
    out[QStringLiteral("x")] = double(position.x);
    out[QStringLiteral("y")] = double(position.y);
    out[QStringLiteral("z")] = double(position.z);
    QVariantMap q;
    q[QStringLiteral("x")] = double(rotation.x);
    q[QStringLiteral("y")] = double(rotation.y);
    q[QStringLiteral("z")] = double(rotation.z);
    q[QStringLiteral("w")] = double(rotation.w);
    out[QStringLiteral("rotation")] = q;
    out[QStringLiteral("yaw")] = double(vrorigin::yawDegrees(rot));
    return out;
}

inline QVariantMap pose(const jahshaka::engine::VrPose &p)
{
    return pose(p.position, p.rotation, p.valid);
}

/// ONE HAND'S WHOLE INPUT AS A VERB ANSWERS IT (phase 4b stage 1,
/// VR_INPUT_SPEC §2.4): the two poses in the shape above and every control as
/// a value beside its press.
///
/// One spelling for the same reason as the rest of this header: `vr.state()
/// .input.left` reports it, the `vr.input.*` verbs report it, and a caller that
/// learned the shape from one must find the same shape in the other.
inline QVariantMap handState(const jahshaka::engine::VrHandState &h)
{
    QVariantMap out;
    out[QStringLiteral("valid")] = h.valid;
    out[QStringLiteral("aim")] = pose(h.aim);
    out[QStringLiteral("grip")] = pose(h.grip);
    // WHERE THE HAND HOLDS THINGS (stage 3): the pinch point on bare fingers,
    // the grip in a fist — the engine's choice from the bound profile, reported
    // so a caller can see which frame a grab is being measured in.
    out[QStringLiteral("manip")] = pose(h.manipPose);
    out[QStringLiteral("profile")] = QString::fromStdString(h.profile);
    out[QStringLiteral("jointsTracked")] = h.jointsTracked;
    out[QStringLiteral("select")] = double(h.select);
    out[QStringLiteral("selectPressed")] = h.selectPressed;
    out[QStringLiteral("grab")] = double(h.grab);
    out[QStringLiteral("grabPressed")] = h.grabPressed;
    out[QStringLiteral("menuPressed")] = h.menuPressed;
    QVariantMap stick;
    stick[QStringLiteral("x")] = double(h.stickX);
    stick[QStringLiteral("y")] = double(h.stickY);
    out[QStringLiteral("stick")] = stick;
    out[QStringLiteral("stickPressed")] = h.stickPressed;
    out[QStringLiteral("fromInjection")] = h.fromInjection;
    // (NO `focused` KEY ON A HAND — VR-INPUT-1E-FIX. The runtime takes input
    // focus away for the whole APPLICATION, never for one hand, so it is
    // reported once, as `vr.state().inputFocused`.)
    return out;
}

/// WHICH HAND A VERB WAS GIVEN: "left"/"l"/0 or "right"/"r"/1, and -1 for
/// anything else (a verb refuses on -1 rather than guessing a hand).
inline int handFrom(const QVariant &raw)
{
    if (raw.typeId() == QMetaType::Int || raw.typeId() == QMetaType::Double ||
        raw.typeId() == QMetaType::LongLong || raw.typeId() == QMetaType::UInt) {
        const int i = raw.toInt();
        return (i == 0 || i == 1) ? i : -1;
    }
    const QString n = raw.toString().trimmed().toLower();
    if (n == QLatin1String("left") || n == QLatin1String("l")) return 0;
    if (n == QLatin1String("right") || n == QLatin1String("r")) return 1;
    return -1;
}

}   // namespace vrnames

#endif   // VRNAMES_H
