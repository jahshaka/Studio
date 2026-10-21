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

#include <QByteArray>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>


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

    /// WHY an asset is being pinned. This is a RULE, not a special case
    /// (LIGHTS_COMPLETION_SPEC D4), and it is deliberately non-defaulted so
    /// that the next feature to bind an asset cannot get it wrong by omission.
    ///
    ///   Direct   — the user added this asset to the project. A Texture added
    ///              this way also gets its companion PBR material created and
    ///              pinned, so it is droppable straight into the viewport.
    ///   Binding  — the asset is pinned because something in the scene REFERS
    ///              to it: a light's IES profile or area mask today, decal maps
    ///              next. A binding is a dependency, exactly like the textures
    ///              riding an object's closure, and never auto-creates a
    ///              companion material. (Without this, binding an image to a
    ///              light would silently spawn a stray material asset.)
    enum class AddKind { Direct, Binding };

    /// Pin `guid` (and its dependency closure) into the project and register
    /// the session AssetManager entries from CAS-resolved bytes. Idempotent.
    static Result addToProject(const QString &guid, Database *db, Project *project,
                               AddKind kind);

    /// ONE SESSION ENTRY'S INPUTS, as the catalog holds them: the row's guid,
    /// type and name, its bytes resolved pin-first (`path`, empty when it has
    /// none on disk) and its definition (`asset`, read for the types whose
    /// session entry carries it — a particle system).
    struct SessionRow
    {
        QString guid;
        int type = 0;
        QString name;
        QString path;
        QByteArray asset;
    };

    /// THE SESSION SET OF A PROJECT, ONE QUERY PER ASSET TYPE (OPEN-PATH-1): the
    /// rows of `type` the project holds — its pins, and for the four legacy
    /// membership types (File, Texture, ParticleSystem, Material) its own rows —
    /// with the pinned bytes (else the library source) resolved in the same
    /// statement and checked on disk here. Runs on ANY thread with a connection
    /// of that thread (the open runner's worker: services/openreads.h); bumps
    /// `*queries` by the statements it issued. `sessionTypes()` are the types
    /// that have a session shape, in the order an open registers them.
    static QVector<SessionRow> readSessionRows(QSqlDatabase conn, const QString &root,
                                               const QString &projectGuid, int type,
                                               int *queries = nullptr);
    static QVector<int> sessionTypes();

    /// THE one session-hydration routine (IMAGE_PLANE_SPEC §6): the AssetManager
    /// entry for one row, built from what the catalog said (no reads here).
    /// Skips a row already registered and a type with no session shape.
    /// `registerSessionAsset` (addToProject's, one guid) and an open's batch
    /// (readSessionRows on the worker, these on the UI thread) both end here, so
    /// the two sessions hydrate identically.
    static bool registerSessionRow(const SessionRow &row);

    /// One guid's registration: its row read (UI thread) and registerSessionRow.
    static bool registerSessionAsset(const QString &guid, Database *db, Project *project);

    /// Move the project's pin of `guid` to the asset's CURRENT source oid
    /// (the "Update to latest" affordance) and pin every member of the new
    /// version the project does not hold yet.
    ///
    /// A member the project ALREADY pins is left exactly where it is (F20):
    /// that pin is either the same bytes or this project's own copy-on-written
    /// version, and updating a BUNDLE is not consent to discard the user's
    /// edited texture. Updating a member is its own gesture on its own tile.
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
