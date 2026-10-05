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

// WHERE A NEW CATALOG ROW LIVES — the typed home (data/assethomekind.h, ASSETS-HOME-1)
// plus the two lookups a gesture needs: the open project's home, and an
// existing row's home (a row born inside another takes its owner's home).

#include <QString>

#include "data/assethomekind.h"
#include "data/database/database.h"
#include "data/project.h"

namespace assethome {

/// The open project's home when one is open, else `otherwise` — for a gesture
/// that is "in the project" exactly when there is a project. The caller names
/// the storage it means when there is none (there is no default "library").
inline Home current(Project *project, const Home &otherwise)
{
    return (project && !project->getProjectGuid().isEmpty())
               ? Home{ Kind::Project, project->getProjectGuid() } : otherwise;
}
/// An EXISTING row's home, as its row records it. A guid that names no row
/// answers `otherwise`.
inline Home of(Database *db, const QString &guid, const Home &otherwise = materials())
{
    if (!db || guid.isEmpty()) return otherwise;
    const AssetRecord row = db->fetchAsset(guid);
    if (row.guid.isEmpty()) return otherwise;
    const Home home = row.home();
    return (home.isProject() && home.projectGuid.isEmpty()) ? otherwise : home;
}

}   // namespace assethome

#endif // ASSETHOME_H
