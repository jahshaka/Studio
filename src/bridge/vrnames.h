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

#include "jahshaka/engine/Engine.h"

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

}   // namespace vrnames

#endif   // VRNAMES_H
