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
// sample scenes reference by path (a Kind::Platform seed row). Star, Wedge,
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
//
// AND IT IS THE SEED LIST (ATOM P2, 2026-09-22). Every row is a shipped mesh
// file that becomes a BAKED library asset — a LOD chain, cards, an SDF, one
// reserved guid — the first time anything asks for it
// (services/primitiveassets.h). Nothing here is parsed at run time any more:
// `Mesh::loadMesh` and its cache are deleted, and with them the ~6 ms of card
// generation every `scene.addPrimitive` paid on the UI thread.

#include <QLatin1String>
#include <QString>
#include <QStringList>
#include <QVector>

namespace primitives {

/// WHAT A ROW IS FOR (ATOM P2). Every row here is a SEED: a shipped mesh file
/// that becomes a baked library asset the first time anything needs it
/// (services/primitiveassets.h). What differs between them is whether a USER
/// may ask for one by name.
enum class Kind
{
    /// The twelve primitives and the Ground: `scene.addPrimitive("Cube")`, the
    /// Add menu, the tiles, the drop payload.
    Primitive,
    /// PLATFORM FURNITURE — seeded and baked exactly the same way, but never
    /// offered: the Teapot, which stopped being a primitive (owner review R6)
    /// while four shipped sample scenes still name its mesh. A user cannot add
    /// one, and it is not a tile.
    ///
    /// (The PREVIEW meshes — the asset dock's high-poly sphere, the material
    /// dock's low-poly ball — are deliberately NOT here. A preview subject is
    /// drawn at one distance in a 256-pixel tile: it has no use for a LOD chain,
    /// cards or an SDF, and making it a library asset would make four
    /// preview-only binaries depend on the whole catalog and import pipeline for
    /// geometry that is a dock's own furniture. They are parsed once per process
    /// through the importer's parse entry point, in bridge/, beside the docks
    /// that own them.)
    Platform,
};

struct Def
{
    /// The name everything calls it by: `scene.addPrimitive("Cube")`, the
    /// tile's caption, the Add menu's entry and the node's own name. For a
    /// Platform row it is only the seed's own label.
    const char *name = nullptr;
    /// THE RESERVED LIBRARY GUID — the row the seeder creates (it is forced,
    /// never minted: ImportRequest::reservedGuid), so a favourite, a drop
    /// payload and `assets.builtins` name the same asset in every library.
    const char *guid = nullptr;
    /// The shipped mesh file the asset is baked FROM, and the key a document
    /// names it by: a Qt resource (":/...", compiled into the binary) or a path
    /// under the app folder ("app/...", resolved with
    /// IrisUtils::getAbsoluteAssetPath). A saved scene stores this string in a
    /// mesh node's `mesh` field exactly as it always did — it is a SEED KEY
    /// now, resolved to the baked asset, never parsed at run time.
    const char *mesh = nullptr;
    /// The tile's icon, relative to the app folder (IrisUtils::
    /// getAbsoluteAssetPath), or nullptr when there is no tile.
    const char *icon = nullptr;
    Kind kind = Kind::Primitive;
};

/// The table. ORDER IS THE TILE ORDER AND THE MENU ORDER — one order, so the
/// drawer and the menu can no longer disagree. It is ALSO the seed list: the
/// meshes a library bakes once (services/primitiveassets.h).
inline const QVector<Def> &all()
{
    static const QVector<Def> defs = {
        // name          guid                                      mesh                                    icon
        { "Ground",      "00000000-0000-0000-0000-000000004012",   ":/models/ground.obj",                  nullptr },
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
        // A PLATFORM SEED (never a tile, never a verb name): the Teapot stopped
        // being a primitive (owner review R6) but four shipped sample SCENES
        // name its mesh, so it is baked like every other mesh a scene stands on.
        { "Teapot",      "00000000-0000-0000-0000-000000004013",   ":/content/primitives/teapot.obj",      nullptr, Kind::Platform },
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

/// The PRIMITIVE row with this name (case-insensitive), or nullptr. A Platform
/// seed is deliberately unreachable here: "Teapot" is a seeded, baked asset the
/// sample scenes still name, and it is NOT a primitive a user may add (owner
/// review R6 — retiredReason above is what they are told instead).
inline const Def *byName(const QString &name)
{
    const QString wanted = name.trimmed().toLower();
    for (const Def &def : all())
        if (def.kind == Kind::Primitive
            && wanted == QString::fromLatin1(def.name).toLower()) return &def;
    return nullptr;
}

/// The row with this guid — old guids included, through canonicalGuid — or
/// nullptr.
inline const Def *byGuid(const QString &guid)
{
    if (guid.isEmpty()) return nullptr;
    const QString wanted = canonicalGuid(guid);
    for (const Def &def : all())
        if (def.guid && wanted == QLatin1String(def.guid)) return &def;
    return nullptr;
}

/// ANY seed row with this name, Platform rows included — the RESOLVER's lookup
/// (services/primitiveassets.h), as opposed to `byName` above, which answers what
/// a USER may add. The two differ by exactly the Teapot: the samples' teapot is a
/// baked asset that has to be resolvable, and `scene.addPrimitive("Teapot")` still
/// has to say no (owner review R6).
inline const Def *bySeedName(const QString &name)
{
    const QString wanted = name.trimmed().toLower();
    for (const Def &def : all())
        if (wanted == QString::fromLatin1(def.name).toLower()) return &def;
    return nullptr;
}

/// THE SEED KEY LOOKUP: the row whose shipped mesh file is `meshKey`, or
/// nullptr. This is how a document's mesh reference — the ":/..." string a scene
/// has always stored for a built-in — reaches its baked asset
/// (services/primitiveassets.h). An EXACT match: the key is a string the writer
/// wrote from this table, and a by-file-name fallback would make two tables of
/// one (it was written for the preview docks, which turned out to own their own
/// furniture instead — bridge/previewmesh.h).
inline const Def *bySeedMesh(const QString &meshKey)
{
    if (meshKey.isEmpty()) return nullptr;
    for (const Def &def : all())
        if (meshKey == QLatin1String(def.mesh)) return &def;
    return nullptr;
}

/// Every primitive NAME, in table order — what `scene.addPrimitive` accepts.
inline QStringList names()
{
    QStringList out;
    for (const Def &def : all())
        if (def.kind == Kind::Primitive) out << QString::fromLatin1(def.name);
    return out;
}

}   // namespace primitives

#endif // DATA_PRIMITIVES_H
