/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETHOME_H
#define ASSETHOME_H

// WHERE A NEW CATALOG ROW LIVES — THE ONE RULE (ASSETS-SCOPE-1, owner
// 2026-09-26): "a project's assets should not be in the Assets module, they
// belong to the project. The Assets module is for things we import or add to
// assets. Adding a material to a project in the editor should add it to the
// editor, not the default assets drawer."
//
// So a row has one of two homes, decided by the gesture that MINTS it:
//
//   LIBRARY  — `AssetViewFilter::AssetsView`, no owning project. ONLY a row
//              born from an IMPORT into the library (the import pipeline's own
//              main rows — assets.import / importFile, the Assets page) or an
//              explicit library gesture (the Materials module's New Material,
//              a shipped preset's seed, materials.create without {folder}, an
//              avatar made at library scope). The Assets module lists these
//              and nothing else (Database::fetchAssetsForAssetView).
//   PROJECT  — `AssetViewFilter::Editor`, `project_guid` = the project. Every
//              row minted while EDITING a project: a material created in the
//              editor, its member textures, its baked maps, a preset's project
//              copy, an image's companion material, a pasted node's assets.
//              The project still PINS it (the pin is membership: the tray, the
//              Materials module's Project drawer and the archive closure read
//              pins), it is simply never a library tile.
//
// A row born INSIDE another (a baked map inside its material, a make-unique
// copy of a material's texture) takes that row's home: `of`.

#include <QString>

#include "data/database/database.h"
#include "data/project.h"

namespace assethome {

struct Home
{
    /// Empty = the library.
    QString projectGuid;

    bool isProject() const { return !projectGuid.isEmpty(); }
    AssetViewFilter viewFilter() const
    {
        return isProject() ? AssetViewFilter::Editor : AssetViewFilter::AssetsView;
    }
};

inline Home library() { return Home{}; }
inline Home project(const QString &projectGuid) { return Home{ projectGuid }; }
/// The open project's home when one is open, else the library's — for a
/// gesture that is "in the project" exactly when there is a project.
inline Home current(Project *project)
{
    return (project && !project->getProjectGuid().isEmpty())
               ? Home{ project->getProjectGuid() } : Home{};
}
/// An EXISTING row's home: a project's own row (Editor, owned) or the library.
inline Home of(Database *db, const QString &guid)
{
    if (!db || guid.isEmpty()) return Home{};
    const AssetRecord row = db->fetchAsset(guid);
    return db->isProjectOwned(row) ? Home{ row.projectGuid } : Home{};
}

}   // namespace assethome

#endif // ASSETHOME_H
