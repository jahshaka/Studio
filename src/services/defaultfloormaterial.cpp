/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/defaultfloormaterial.h"

#include "data/database/database.h"
#include "data/project.h"
#include "irisgl/core/logger.h"
#include "services/shippedassets.h"

namespace defaultfloormaterial {

QString pinTile(Database *db, Project *project, QString *tileGuid, bool *tileNewlyPinned)
{
    // THE GROUND'S TILE IS A LIBRARY TEXTURE (plan item 15c, audit D35). In a
    // real project it goes through the one import pipeline the first time any
    // project needs it (identified by its bytes, so every later project reuses
    // the same row) and is PINNED here: the material holds the pinned store
    // object and the map row carries its guid to the save (TEX-REF-1), both readers
    // resolve it pin-first — the same round trip as any texture a user
    // imports, and a project export carries it.
    //
    // PINNED, BUT NOT THE USER'S (owner, 2026-09-13: "the project asset tray
    // should only show assets and items added to the project"). The pin has to
    // stay — it is what makes `material.reset` find the checker again and what
    // puts the bytes in a project archive, so a fresh box importing that
    // archive still renders a checkered floor — so the row is marked
    // PLATFORM-owned instead, and the tray drops it by the same rule that
    // drops a built-in node's own row (services/assettray.h rule 4). Not
    // pinning it at all was the other option and it fails the archive: the
    // membership sweep is "the project's rows plus its pins"
    // (services/projectarchiver.cpp), so an unpinned tile travels with nothing
    // and the floor arrives untextured on a machine whose library never had
    // it.
    //
    // The STARTUP PLACEHOLDER project (Project::createNew: no guid, no folder)
    // never saves, so it renders the shipped file directly and writes nothing
    // to the library.
    QString tilePath = shippedTilePath();
    if (tileGuid) tileGuid->clear();
    if (tileNewlyPinned) *tileNewlyPinned = false;
    if (db && project && !project->getProjectGuid().isEmpty()) {
        const ShippedAssets::Pinned tile =
            ShippedAssets::pinTexture(tilePath, QStringLiteral("Tile.png"), db, project,
                                      ShippedAssets::Ownership::Platform);
        if (tile.ok() && !tile.guid.isEmpty()) {
            tilePath = tile.path;
            if (tileGuid) *tileGuid = tile.guid;
            if (tileNewlyPinned) *tileNewlyPinned = tile.newlyPinned;
        } else {
            // The floor still renders (the shipped file); what is lost is the
            // guid on save, so say why instead of saving a scene that reopens
            // untextured with nothing in the log.
            irisLog("defaultfloormaterial: the floor tile could not be pinned into the project - "
                    + tile.error);
        }
    }

    return tilePath;
}

iris::PbrMaterialPtr create(Database *db, Project *project, QString *tileGuid,
                            bool *tileNewlyPinned)
{
    QString guid;
    const QString path = pinTile(db, project, &guid, tileNewlyPinned);
    if (tileGuid) *tileGuid = guid;
    return createUnpinned(path, guid);
}

}   // namespace defaultfloormaterial
