/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETHOMEKIND_H
#define ASSETHOMEKIND_H

// WHERE A CATALOG ROW LIVES — the TYPED HOME every row is minted with
// (ASSETS-HOME-1; the owner, 2026-09-26 and 2026-10-04).
//
//   ASSETS            long-term storage. Changes ONLY on an import or an explicit
//                     "Save to Assets": a row is legal here only with the origin
//                     Import or ExplicitSave (`refusal` below, enforced at the one
//                     door, Database::createAssetEntry).
//   MATERIALS LIBRARY the Materials module's own storage: every material a user
//                     creates there, IN FULL (the bundle and its member maps).
//   AVATAR LIBRARY    the Avatar module's own storage: the avatars made in it.
//   PROJECT(guid)     a project's own row (the editor's gestures).
//   PLATFORM          what the app ships and seeds itself: the primitives, the
//                     material presets and their maps, the floor checker. Never
//                     a tile, never anybody's storage; re-seeded after a reset.
//
// The `view_filter` column IS the home (one value per kind, below) and is never
// written from anywhere but a Home: there is no second flag that could disagree
// with it, and a non-project home never carries a project guid.
//
// A row born INSIDE another (a mesh inside its model, a baked map inside its
// material, a member texture) takes its owner's home.

#include <QString>

namespace assethome {

enum class Kind
{
    Project,
    Assets,
    MaterialsLibrary,
    AvatarLibrary,
    Platform
};

/// HOW a row came to exist — the half of the door rule Assets checks.
enum class Origin
{
    Import,         ///< the user brought a file in (the import pipeline)
    ExplicitSave,   ///< the user said "Save to Assets" (or pasted into the Assets page)
    Create          ///< made by a gesture or a seed (a new material, a copy, a primitive)
};

/// The stored value of each home (`assets.view_filter`). Stable numbers: they
/// ride the sidecars, and a sidecar records the home BY NAME as well.
enum StoredHome : int
{
    StoredProject = 1,
    StoredAssets = 2,
    StoredPlatform = 4,
    StoredMaterialsLibrary = 5,
    StoredAvatarLibrary = 6
};

struct Home
{
    Kind kind = Kind::Project;   ///< deliberately an INVALID default: a Project with no guid
    QString projectGuid;         ///< set exactly when kind == Project

    bool isProject() const { return kind == Kind::Project; }
    /// One of the user's long-term storages (kept across a format bump).
    bool isStorage() const
    {
        return kind == Kind::Assets || kind == Kind::MaterialsLibrary
               || kind == Kind::AvatarLibrary;
    }
    int stored() const
    {
        switch (kind) {
        case Kind::Project:          return StoredProject;
        case Kind::Assets:           return StoredAssets;
        case Kind::MaterialsLibrary: return StoredMaterialsLibrary;
        case Kind::AvatarLibrary:    return StoredAvatarLibrary;
        case Kind::Platform:         return StoredPlatform;
        }
        return StoredProject;
    }
    bool operator==(const Home &other) const
    {
        return kind == other.kind && (kind != Kind::Project || projectGuid == other.projectGuid);
    }
    bool operator!=(const Home &other) const { return !(*this == other); }
};

inline Home assets() { return Home{ Kind::Assets, QString() }; }
inline Home materials() { return Home{ Kind::MaterialsLibrary, QString() }; }
inline Home avatars() { return Home{ Kind::AvatarLibrary, QString() }; }
inline Home platform() { return Home{ Kind::Platform, QString() }; }
inline Home project(const QString &projectGuid) { return Home{ Kind::Project, projectGuid }; }

/// The home a row of THIS schema records. A value no home writes reads as an
/// invalid (guid-less) Project home, which every door refuses.
inline Home fromStored(int viewFilter, const QString &projectGuid)
{
    switch (viewFilter) {
    case StoredProject:          return project(projectGuid);
    case StoredAssets:           return assets();
    case StoredMaterialsLibrary: return materials();
    case StoredAvatarLibrary:    return avatars();
    case StoredPlatform:         return platform();
    default:                     return Home{};
    }
}

/// True for a value that names one of the user's storages.
inline bool isStorageValue(int viewFilter)
{
    return viewFilter == StoredAssets || viewFilter == StoredMaterialsLibrary
           || viewFilter == StoredAvatarLibrary;
}

inline QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Project:          return QStringLiteral("project");
    case Kind::Assets:           return QStringLiteral("assets");
    case Kind::MaterialsLibrary: return QStringLiteral("materials");
    case Kind::AvatarLibrary:    return QStringLiteral("avatars");
    case Kind::Platform:         return QStringLiteral("platform");
    }
    return QString();
}

inline bool kindFromName(const QString &name, Kind *out)
{
    for (Kind k : { Kind::Project, Kind::Assets, Kind::MaterialsLibrary, Kind::AvatarLibrary,
                    Kind::Platform })
        if (kindName(k) == name) { *out = k; return true; }
    return false;
}

inline QString originName(Origin origin)
{
    switch (origin) {
    case Origin::Import:       return QStringLiteral("import");
    case Origin::ExplicitSave: return QStringLiteral("save");
    case Origin::Create:       return QStringLiteral("create");
    }
    return QString();
}

inline bool originFromName(const QString &name, Origin *out)
{
    for (Origin o : { Origin::Import, Origin::ExplicitSave, Origin::Create })
        if (originName(o) == name) { *out = o; return true; }
    return false;
}

/// THE ORIGIN OF A ROW BORN INSIDE ANOTHER (a baked map, a member copy, a
/// mesh): it is part of its owner, so it carries the owner's kind of arrival —
/// inside an Assets bundle that is the user's save, anywhere else a creation.
inline Origin bornInside(const Home &ownerHome)
{
    return ownerHome.kind == Kind::Assets ? Origin::ExplicitSave : Origin::Create;
}

/// THE DOOR RULE, as a sentence for the caller, or empty when (home, origin) is
/// legal. Asserted by tests/assethome.
inline QString refusal(const Home &home, Origin origin)
{
    if (home.isProject() && home.projectGuid.isEmpty())
        return QStringLiteral("a project row needs its project's guid");
    if (!home.isProject() && !home.projectGuid.isEmpty())
        return QStringLiteral("only a project row carries a project guid");
    if (home.kind == Kind::Assets && origin != Origin::Import && origin != Origin::ExplicitSave)
        return QStringLiteral("Assets holds only what the user imports or saves there "
                              "(origin '%1' is neither)").arg(originName(origin));
    return QString();
}

}   // namespace assethome

#endif // ASSETHOMEKIND_H
