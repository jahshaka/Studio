/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef DATA_PRIMITIVES_H
#define DATA_PRIMITIVES_H

// THE SHIPPED PRIMITIVES, AS ONE TABLE (owner review R6 / answer Q4, 2026-09-18).
//
// There were FOUR lists of them, plus a fifth partial one in the hierarchy's
// Add menu, and they had already drifted apart:
//
//   * `Constants::Reserved::DefaultPrimitives` — the library guids (the drop
//     payload and `assets.builtins`), whose guids COLLIDED with
//     BuiltinShaders' 0002-0006 (harmless only because every lookup happens to
//     be type-scoped, which is not a property anybody checks when adding a row)
//   * `SceneEditService`'s `kPrimitiveDefs` — name -> mesh -> node name
//   * `AssetModelPanel`'s `defaultModels` — name -> mesh -> tile icon, in its
//     own order, missing Pyramid and Ground
//   * `SceneApi`'s `kPrimitives` — the names the verb accepts, in a third order
//   * `SceneHierarchyWidget`'s Add > Primitive menu — six of the twelve,
//     hand-written, so Cone / Capsule / Pyramid were unreachable from the menu
//
// One table, here, with every column any of them needed. Header-only for the
// same reason AppPaths is: these rows are read from src/data, src/services,
// src/ui, src/viewport and src/scripting, which are compiled into a dozen test
// targets that each list their own sources — a .cpp would mean one more line in
// every one of them and a link error for whoever forgets.
//
// WHAT IS IN IT (the owner's Q4 answer, verbatim in intent): PRIMITIVES ONLY.
// Gear, Sponge and Steps are DELETED — test-era models, not primitives. The
// Teapot leaves the tiles the same way and stays only as the mesh four shipped
// sample scenes reference by path (see kSampleOnlyMeshes below). Star, Wedge,
// Tube and Hemisphere are new and are real generated geometry. World-building
// sets ("wall, ramp, stairs, arch") are NOT here and never will be: they come
// later as asset collections the user downloads.
//
// THE GUIDS ARE THEIR OWN RANGE NOW (…00000000 4000 and up). They used to sit
// on top of BuiltinShaders'. Nothing is owed to old data (the crud law), but a
// library row minted before the renumber can still name an old guid — a
// FAVOURITE, which is the one place a primitive guid is persisted — so
// `canonicalGuid` maps old -> new, in ONE place, and every lookup here goes
// through it.

#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <QVector>

namespace primitives {

struct Def
{
    /// The name everything calls it by: `scene.addPrimitive("Cube")`, the
    /// tile's caption, the Add menu's entry and the node's own name.
    const char *name = nullptr;
    /// The reserved library guid, or nullptr for a row that has no library
    /// tile (Ground — it is an Add-menu entry and a verb name, never a tile:
    /// it is 100 m of floor, not something to drag into a drawer).
    const char *guid = nullptr;
    /// The bundled mesh, as a Qt resource path.
    const char *mesh = nullptr;
    /// The tile's icon, relative to the app folder (IrisUtils::
    /// getAbsoluteAssetPath), or nullptr when there is no tile.
    const char *icon = nullptr;
};

/// The table. ORDER IS THE TILE ORDER AND THE MENU ORDER — one order, so the
/// drawer and the menu can no longer disagree.
inline const QVector<Def> &all()
{
    static const QVector<Def> defs = {
        // name          guid                                      mesh                                    icon
        { "Ground",      nullptr,                                  ":/models/ground.obj",                  nullptr },
        { "Plane",       "00000000-0000-0000-0000-000000004000",   ":/content/primitives/plane.obj",       "app/modelpresets/plane.png" },
        { "Cube",        "00000000-0000-0000-0000-000000004001",   ":/content/primitives/cube.obj",        "app/modelpresets/cube.png" },
        { "Sphere",      "00000000-0000-0000-0000-000000004002",   ":/content/primitives/sphere.obj",      "app/modelpresets/sphere.png" },
        { "Hemisphere",  "00000000-0000-0000-0000-000000004003",   ":/content/primitives/hemisphere.obj",  "app/modelpresets/hemisphere.png" },
        { "Cylinder",    "00000000-0000-0000-0000-000000004004",   ":/content/primitives/cylinder.obj",    "app/modelpresets/cylinder.png" },
        { "Tube",        "00000000-0000-0000-0000-000000004005",   ":/content/primitives/tube.obj",        "app/modelpresets/tube.png" },
        { "Cone",        "00000000-0000-0000-0000-000000004006",   ":/content/primitives/cone.obj",        "app/modelpresets/cone.png" },
        { "Pyramid",     "00000000-0000-0000-0000-000000004007",   ":/content/primitives/pyramid.obj",     "app/modelpresets/pyramid.png" },
        { "Torus",       "00000000-0000-0000-0000-000000004008",   ":/content/primitives/torus.obj",       "app/modelpresets/torus.png" },
        { "Capsule",     "00000000-0000-0000-0000-000000004009",   ":/content/primitives/capsule.obj",     "app/modelpresets/capsule.png" },
        { "Wedge",       "00000000-0000-0000-0000-000000004010",   ":/content/primitives/wedge.obj",       "app/modelpresets/wedge.png" },
        { "Star",        "00000000-0000-0000-0000-000000004011",   ":/content/primitives/star.obj",        "app/modelpresets/star.png" },
    };
    return defs;
}

/// THE RETIRED NAMES, and what to say about them. A user's script (or a
/// habit) that still asks for one gets told it is gone and where it went,
/// instead of the verb quietly doing nothing — which is what the old
/// fall-through loop did for any name it did not recognise.
inline QString retiredReason(const QString &name)
{
    const QString wanted = name.trimmed().toLower();
    if (wanted == QLatin1String("gear") || wanted == QLatin1String("sponge")
        || wanted == QLatin1String("steps"))
        return QStringLiteral("'%1' is no longer a built-in primitive — Gear, Sponge and Steps "
                              "were test models, not primitives, and were removed (owner, "
                              "2026-09-18). Import a model, or build it from primitives.")
            .arg(name.trimmed());
    if (wanted == QLatin1String("teapot"))
        return QStringLiteral("'Teapot' is no longer a built-in primitive — it belongs to the "
                              "sample scenes that use it (owner, 2026-09-18). The samples still "
                              "open with it; it is not something new scenes are offered.");
    return QString();
}

/// Old primitive guid -> the guid it has now. Everything else comes back
/// unchanged, so this is safe to put in front of EVERY guid lookup — which is
/// what makes it the one place the mapping lives.
inline QString canonicalGuid(const QString &guid)
{
    // The pre-2026-09-19 table, in its own order. The three retired rows
    // (…0007 Gear, …0010 Sponge, …0011 Steps) and the Teapot (…0009) map to
    // NOTHING: they are not primitives any more, and answering with some other
    // shape would be worse than answering with nothing.
    struct Moved { const char *from = nullptr; const char *to = nullptr; };
    static const Moved moved[] = {
        { "00000000-0000-0000-0000-000000001000", "00000000-0000-0000-0000-000000004000" },  // Plane
        { "00000000-0000-0000-0000-000000001001", "00000000-0000-0000-0000-000000004006" },  // Cone
        { "00000000-0000-0000-0000-000000000002", "00000000-0000-0000-0000-000000004001" },  // Cube
        { "00000000-0000-0000-0000-000000000003", "00000000-0000-0000-0000-000000004004" },  // Cylinder
        { "00000000-0000-0000-0000-000000000004", "00000000-0000-0000-0000-000000004002" },  // Sphere
        { "00000000-0000-0000-0000-000000000005", "00000000-0000-0000-0000-000000004008" },  // Torus
        { "00000000-0000-0000-0000-000000000006", "00000000-0000-0000-0000-000000004009" },  // Capsule
        { "00000000-0000-0000-0000-000000000008", "00000000-0000-0000-0000-000000004007" },  // Pyramid
    };
    for (const Moved &m : moved)
        if (guid == QLatin1String(m.from)) return QString::fromLatin1(m.to);
    return guid;
}

/// The row with this name (case-insensitive), or nullptr.
inline const Def *byName(const QString &name)
{
    const QString wanted = name.trimmed().toLower();
    for (const Def &def : all())
        if (wanted == QString::fromLatin1(def.name).toLower()) return &def;
    return nullptr;
}

/// The row with this guid — old guids included, through canonicalGuid — or
/// nullptr. Rows with no guid (Ground) are never matched.
inline const Def *byGuid(const QString &guid)
{
    if (guid.isEmpty()) return nullptr;
    const QString wanted = canonicalGuid(guid);
    for (const Def &def : all())
        if (def.guid && wanted == QLatin1String(def.guid)) return &def;
    return nullptr;
}

/// Every name, in table order — what `scene.addPrimitive` accepts.
inline QStringList names()
{
    QStringList out;
    for (const Def &def : all()) out << QString::fromLatin1(def.name);
    return out;
}

/// Every mesh the table names, plus the meshes only the SHIPPED SAMPLES use.
///
/// `Mesh::pinLoadPaths` holds these parsed for the life of the process
/// (OPEN-ASSIMP-1): they are a few kilobytes each, compiled into the binary,
/// and the mesh cache holds WEAK references, so without the pin closing a world
/// drops them and the next open re-parses them ON THE UI THREAD. The Teapot is
/// in the second list precisely because it stopped being a primitive: four
/// shipped samples still name `:/content/primitives/teapot.obj` in their scene
/// blobs, and dropping the pin with the tile would have handed every open of
/// those four a 111 KB parse on the UI thread.
inline QStringList pinnedMeshPaths()
{
    QStringList out;
    for (const Def &def : all()) out << QString::fromLatin1(def.mesh);
    out << QStringLiteral(":/content/primitives/teapot.obj");
    return out;
}

}   // namespace primitives

#endif // DATA_PRIMITIVES_H
