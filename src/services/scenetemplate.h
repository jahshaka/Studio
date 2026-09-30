/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENETEMPLATE_H
#define SCENETEMPLATE_H

// THE NEW-SCENE TEMPLATES (WORLD-MODEL-1, owner 2026-09-30: "like Unreal").
// The ONE place the set is named: the New Scene dialog's drop-down, the
// `project.create({template})` verb and MainWindow::createDefaultScene all
// speak this enum, and the verb's strings are the ones below.
//
//   * Basic — the default: the realistic sky, the sun, the Sky Light and ONE
//     floor, an ordinary cube node named "Floor" (100 x 1 x 100 m, its top
//     face at y = 0) wearing the default floor material;
//   * Empty — NOTHING: the root node, the Epic tier, no sky, no lights, no
//     floor;
//   * World — Basic's sky and lights, and a group "World Floor" holding
//     twenty-five of Basic's floor cubes, 5 x 5, edge to edge (500 m square),
//     standing in for terrain.
//
// Every floor is an ordinary node that SHIPS LOCKED (not pickable — owner,
// 2026-09-30): unlock it, then select, move, delete or re-material it. The
// dialog's fourth entry, "Sample", is UI only (disabled) until it exists — it
// has no value here on purpose.

#include <QString>
#include <QStringList>

enum class SceneTemplate
{
    Basic,
    Empty,
    World,
};

namespace scenetemplate {

/// The verb's spelling: "basic", "empty", "world".
inline QString name(SceneTemplate t)
{
    switch (t) {
    case SceneTemplate::Empty: return QStringLiteral("empty");
    case SceneTemplate::World: return QStringLiteral("world");
    case SceneTemplate::Basic: break;
    }
    return QStringLiteral("basic");
}

/// Every template, in the dialog's order.
inline QStringList names()
{
    return { name(SceneTemplate::Basic), name(SceneTemplate::Empty), name(SceneTemplate::World) };
}

/// Parses the verb's spelling (case-insensitive, trimmed). False, and `out`
/// untouched, for anything else — the caller refuses it by name.
inline bool fromName(const QString &text, SceneTemplate *out)
{
    const QString t = text.trimmed().toLower();
    for (SceneTemplate v : { SceneTemplate::Basic, SceneTemplate::Empty, SceneTemplate::World }) {
        if (t == name(v)) { if (out) *out = v; return true; }
    }
    return false;
}

/// The floor every template builds from: the cube primitive (a 2 m cube)
/// scaled to 100 x 1 x 100 m, and how many of them a World lays per side.
constexpr float kFloorSize = 100.0f;
constexpr float kFloorThickness = 1.0f;
constexpr int   kWorldTilesPerSide = 5;

}   // namespace scenetemplate

#endif   // SCENETEMPLATE_H
