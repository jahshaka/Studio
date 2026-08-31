/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECTASSETS_H
#define PROJECTASSETS_H

// Reference-with-pin project membership (ASSET_PIPELINE_SPEC §3.1.5,
// phase 4). Adding an asset to a project is a project_assets ROW pinning
// {asset guid, source oid at add time} — no file copies, no row cloning:
// the scene references the LIBRARY guid directly, and content immutability
// (I3) makes the pin behave like a private copy. A library re-import moves
// the library pointer; the project keeps rendering its pinned bytes until
// updatePinToLatest. Per-project edits are copy-on-write: new bytes become
// a new oid, the pin moves, the library asset never changes.
//
// This is THE one implementation behind AssetsApi::addToProject and
// AssetView::addAssetItemToProject — the twin ~130-line transcriptions
// (flat folder copies + Database::copyAsset clones) died with it.

#include <QString>
#include <QStringList>

class Database;
class Project;

class ProjectAssets
{
public:
    struct Result
    {
        QString guid;             // the asset's guid — NOT a clone
        QString error;
        QStringList pinnedGuids;  // asset + dependencies that got pin rows
        bool ok() const { return error.isEmpty() && !guid.isEmpty(); }
    };

    /// Pin `guid` (and its dependency closure) into the project and register
    /// the session AssetManager entries from CAS-resolved bytes. Idempotent.
    static Result addToProject(const QString &guid, Database *db, Project *project);

    /// THE one session-hydration routine (IMAGE_PLANE_SPEC §6): registers the
    /// AssetManager entry for `guid`, bytes resolved pin-first through the CAS
    /// (AssetCas::resolvePinned falls back to the library source). Skips guids
    /// already registered, unknown guids and types with no session shape.
    /// Called by addToProject (the adding session) and by
    /// ProjectManager::registerProjectSessionAssets (project open/reopen) so
    /// the two sessions hydrate identically.
    static bool registerSessionAsset(const QString &guid, Database *db, Project *project);

    /// Move the project's pin of `guid` to the asset's CURRENT source oid
    /// (the "Update to latest" affordance).
    static bool updatePinToLatest(const QString &guid, Database *db, Project *project);

    /// Copy-on-write: `newContentPath`'s bytes become a new CAS object
    /// recorded under the asset (role "source" stays; the edit is a new
    /// asset_files row only if the name changed) and the PROJECT pin moves
    /// to it. The library pointer (and every other project's pin) is
    /// untouched. Returns the new oid, empty on failure.
    static QString copyOnWrite(const QString &guid, const QString &newContentPath,
                               Database *db, Project *project, QString *errorOut = nullptr);
};

#endif // PROJECTASSETS_H
