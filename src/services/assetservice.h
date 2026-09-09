/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETSERVICE_H
#define ASSETSERVICE_H

// AssetService — the asset-store operations (APP_ARCHITECTURE_AUDIT §3.3).
//
// Grows around AssetImporter (the first service to escape MainWindow's orbit,
// now living beside it in src/services/). Phase 1 exposes the headless mesh
// import; AssetView's add/delete/rename sweeps drain into it in later phases
// (audit §3.3 "AssetService" row). Constructor-injected, QObject-free.

#include <QString>
#include <functional>
#include <vector>

#include "services/assetimporter.h"

class Database;
class Project;

class AssetService
{
public:
    AssetService(Database *db, Project *project) : db(db), project(project) {}

    /// Imports a mesh file (obj/fbx/dae/blend/glb/gltf) into the global asset
    /// store. Pure document/DB work — safe headless.
    AssetImporter::Result importMesh(const QString &filePath)
    {
        return AssetImporter::importMesh(filePath, db, project);
    }

    /// Imports any library-supported file (models, images, audio) into the
    /// global asset store, optionally filed in a drawer (ASSET_DRAWERS_SPEC
    /// §3). Images/audio are headless-safe.
    /// `typeHint` (a ModelTypes value, -1 = sniff from the file) rides through
    /// to the pipeline's ImportRequest — assets.importFile's {typeHint}.
    AssetImporter::Result importFile(const QString &filePath, int drawerId = -1,
                                     int typeHint = -1)
    {
        return AssetImporter::importFile(filePath, db, project, drawerId, typeHint);
    }

    // ---- THE PIN-CHANGE ANNOUNCEMENT (AVATAR_ASSET_SPEC §4 D4) ------------
    //
    // Every move of a project's pin — add to project, update from library, a
    // copy-on-write save — changes WHICH BYTES this project's instances of
    // that asset are made of. Linked avatar instances have to re-resolve, and
    // they have to do it NOW rather than at the next scene open, or the module
    // saves a clip and the character standing in the viewport ignores it.
    //
    // A plain callback list rather than a Qt signal: this service is
    // deliberately QObject-free (it is constructed and injected, not parented),
    // and the one consumer is a service, not a widget. Subscriptions live for
    // the life of the service; nothing here unsubscribes, so a subscriber must
    // outlive it or capture weakly.
    using PinChangedFn = std::function<void(const QString &assetGuid)>;
    void onPinChanged(PinChangedFn callback)
    {
        if (callback) pinSubscribers.push_back(std::move(callback));
    }
    void announcePinChanged(const QString &assetGuid)
    {
        if (assetGuid.isEmpty()) return;
        // Iterate a COPY: a subscriber that reacts by adding another asset to
        // the project (the closure walk does) would otherwise invalidate the
        // vector mid-loop.
        const auto subscribers = pinSubscribers;
        for (const auto &callback : subscribers) callback(assetGuid);
    }

private:
    Database *db;
    Project *project;   // the live Project (Phase 4: was Globals::project)
    std::vector<PinChangedFn> pinSubscribers;
};

#endif // ASSETSERVICE_H
