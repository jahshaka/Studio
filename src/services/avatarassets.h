/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef AVATARASSETS_H
#define AVATARASSETS_H

// Avatar assets (AVATAR_ASSET_SPEC §5.2) — THE one implementation behind
// every avatar verb and widget, the way `ProjectAssets` is for pins.
//
// An avatar asset is a `ModelTypes::Avatar` library row whose `source` file is
// the avatar DEFINITION (irisgl/document/assets/avatardefinition.h). Because
// it is an ordinary row with an ordinary source file, the whole asset pipeline
// applies with no changes: `project_assets` pins it per project, `copyOnWrite`
// gives the project its own version on the first project-side edit, the
// archive walkers carry it, `assets.gc` reaps superseded versions.
//
// THE TWO SCOPES, which every entry point here takes explicitly:
//
//   Library — the row itself. A save publishes a new version: the definition's
//             bytes become a new object and the library's `source` pointer
//             moves to it. NO project's pin moves; a project that pinned the
//             old version keeps rendering it until Update from Library.
//   Project — the open project's version of the row. A save is a COPY-ON-WRITE:
//             the bytes become a new object and only THIS project's pin moves.
//             The library and every other project are untouched.
//
// That is the owner's model exactly: "Add to Project creates a unique version
// — asset edits go to the asset, project edits go to the project", implemented
// as the pipeline's own pin/COW semantics rather than as a copy (§4 D3).

#include <QString>

#include "irisgl/document/assets/avatardefinition.h"

class Database;
class Project;

class AvatarAssets
{
public:
    enum class Scope
    {
        Library,   ///< the library row: a save publishes a new version
        Project    ///< the open project's version: a save is copy-on-write
    };

    /// Parses `"library"` / `"project"` (case-insensitive). False on anything
    /// else — a typo'd scope must refuse, never silently edit the wrong one.
    static bool scopeFromName(const QString &name, Scope &out);
    static QString scopeName(Scope scope);

    struct Loaded
    {
        iris::AvatarDefinition definition;
        QString oid;      ///< the content id this definition was read from
        QString name;     ///< the library ROW's name
        QString error;
        bool ok() const { return error.isEmpty(); }
    };

    /// Mint an avatar asset from a rigged model Object (§4 D8: lazily, from
    /// "Create Avatar" / "Edit in Avatar Module"). The definition lists the
    /// model's own in-file clips and defaults to the first. Refuses an Object
    /// with no skeleton — with the bone count in the message, because "this
    /// model is not rigged" is useless when the user believes it is.
    ///
    /// Library scope mints a library row; Project scope mints a row owned by
    /// the open project AND pins it (§4 D6 promotes it later).
    static QString create(const QString &objectGuid, Scope scope, Database *db, Project *project,
                          const QString &nameOverride, QString *errorOut);

    /// Read the definition at the scope's version: Library = the row's current
    /// source, Project = the project's pinned oid.
    static Loaded load(const QString &guid, Scope scope, Database *db, Project *project);

    /// Write the definition at the scope (see the two-scope note above) and
    /// reconcile the asset's dependency rows to the definition's model + clips.
    /// Returns the new content id, empty on failure.
    static QString save(const QString &guid, Scope scope, const iris::AvatarDefinition &definition,
                        Database *db, Project *project, QString *errorOut);

    /// Publish the project's version as the library's current one (§1's "Save
    /// to Library"): the library `source` pointer moves to the pinned oid, the
    /// project keeps its pin — same bytes, now shared. An avatar that was
    /// CREATED in-project is PROMOTED IN PLACE instead (§4 D6-A): the row keeps
    /// its guid and loses its project ownership, so every instance already
    /// pointing at it stays valid.
    static QString saveToLibrary(const QString &guid, Database *db, Project *project,
                                 QString *errorOut);

    /// The library's current source oid for `guid` (no project involved).
    static QString libraryVersion(const QString &guid);
    /// The open project's pinned oid for `guid`, empty when it pins nothing.
    static QString projectVersion(const QString &guid, Project *project);
    /// True when the project's version differs from the library's current one
    /// — the module's `[edited]` marker.
    static bool isEdited(const QString &guid, Project *project);

    /// Is this row an avatar? (A guid that names something else must refuse in
    /// the verb, not be treated as an empty avatar.)
    static bool isAvatarRow(const QString &guid, Database *db);

private:
    /// Write `definition` to a staging file and return its path; empty on
    /// failure. The caller ingests it and removes the staging dir.
    static QString stageDefinition(const iris::AvatarDefinition &definition,
                                   const QString &fileName, QString *errorOut);
    static void reconcileDependencies(const QString &guid, const iris::AvatarDefinition &definition,
                                      Database *db, Project *project);
};

#endif   // AVATARASSETS_H
