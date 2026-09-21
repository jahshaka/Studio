/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECTFOLDERS_H
#define PROJECTFOLDERS_H

// THE PROJECT'S FOLDERS — ONE model, one set of rules (DRAWERS-1, owner review
// 2026-09-18 item 10: "create folders inside the drawer and move assets into
// folders there so we can organise the drawers properly").
//
// A project folder is a row of the `folders` table with a `parent` (another
// folder's guid, or the project's guid for the root). Folders already existed
// — the editor files its own hidden ones (Systems, Presets) through
// Database::ensureFolder, and the tray listed and created them — but there was
// no verb for any of it and TWO widgets carried their own copy of the create
// (ui/panels/assetwidget.cpp and modules/materials/widgets/shaderassetwidget.
// cpp, which minted rows nothing navigated to). This is that model as one
// service: the verbs (assets.folders / createFolder / renameFolder /
// deleteFolder / moveToFolder) are thin forwarders and both drawers are views.
//
// WHERE A ROW IS FILED, AND WHY IT IS TWO COLUMNS. The editor tray lists two
// kinds of row (services/assettray.h): rows the PROJECT owns (a scene node's
// own row, a preset it filed) and LIBRARY rows the project PINS. A library row
// is shared by every project, so its filing cannot live in `assets.parent` —
// writing a folder guid there would file the same asset into one project's
// folder for everybody, and the row would then vanish from every listing
// (`fetchChildAssets` matches on project_guid, which a library row does not
// have). A pin's filing is therefore a column on the PIN: `project_assets.
// folder` (Database::migrateProjectAssetsTable adds it in place). This service
// is the only place that has to know which of the two a guid takes — `file`
// decides, `folderOf` answers, and everything above them speaks in guids.
//
// AN ARCHIVE CARRIES BOTH FILINGS (ARCHIVE-FOLDER-1; this was a stated gap
// until then). A row the project OWNS keeps its folder because that rides
// `assets.parent` inside the archive's catalog snapshot; a PINNED row's folder
// rides the export manifest (`folder` per asset, exportmanifest.h) and is
// written back by the importer right after the pin. The folder ROW itself
// travels in the catalog and keeps its guid, so the recorded guid still names
// it on the far side. An archive written before the key carries no folder and
// its pins come back at the root, exactly as they used to.
//
// REFERENCE-WITH-PIN IS UNCHANGED. A move changes where a row is LISTED and
// nothing else: no pin moves, no content is re-resolved, no bytes are touched.
//
// ONE ANNOUNCEMENT. Every verb that changes the listing calls `announce`,
// which is ProjectMembership (services/projectmembership.h) — the signal the
// editor tray has repopulated from since lane L13, and which the Materials
// module's project drawer now listens to as well. That is the owner's "both
// drawers refresh from one signal".

#include <QString>
#include <QStringList>
#include <QVector>

class Database;

namespace projectfolders {

/// One folder of a project's tree. `count` is how many ROWS the folder holds —
/// the UNION of the rows the project owns and the pins filed there, never
/// their sum: a row is very often both (an import made with a project open
/// writes the project's guid on the row and addToProject then pins it), and
/// the two filing columns describe ONE asset. Not a recursive total, and not
/// the tray's collapsed TILE count (a row the tray folds away, an import
/// member, still counts as something filed here).
struct Info
{
    QString guid;
    QString name;
    QString parent;
    int     count = 0;
};

/// The answer of every mutating call: what happened, and in the caller's own
/// words when it did not. `guid` is the new folder (create); `moved` is how
/// many rows a move actually re-filed.
struct Result
{
    bool    ok = false;
    QString error;
    QString guid;
    int     moved = 0;
};

/// A FOLDER THE EDITOR OWNS BY NAME — today exactly one, `Systems`, which
/// holds a row per particle emitter and which SceneEditService finds through
/// `Database::ensureFolder`, by NAME. Renaming or deleting it, minting a user
/// folder that takes the name, or MOVING it somewhere the editor does not
/// expect all break the writer that looks it up, so all four directions are
/// refused here. ("Presets" used to be a second one and is dead — nothing has
/// ensured it since a preset became an ordinary library bundle,
/// services/materialpresetassets.h.)
bool isSystemFolder(const QString &name);

/// The project's folders: every one of them, or (with `parent` set) just that
/// parent's children. The project's own guid names the ROOT.
QVector<Info> list(Database *db, const QString &projectGuid,
                   const QString &parent = QString());

/// WOULD IT BE ACCEPTED? The same rules and the same words as `create`, with
/// nothing written — so a caller that wraps the create in an UNDO COMMAND can
/// ask before it pushes. A refused command that is pushed anyway becomes a
/// no-op step on the user's undo stack: one Ctrl+Z that does nothing at all.
Result judgeCreate(Database *db, const QString &projectGuid, const QString &name,
                   const QString &parent = QString());

/// Creates a folder under `parent` (empty = the project root). Refuses an
/// empty name, a system name, an unknown parent and a duplicate name under
/// the same parent. `guid` lets a caller (the undo command) re-create the
/// SAME folder on a redo; empty mints one.
Result create(Database *db, const QString &projectGuid, const QString &name,
              const QString &parent = QString(), const QString &guid = QString());

/// Renames a folder. Refuses an empty name, a system folder, an unknown
/// folder, one belonging to another project, and a duplicate under its parent.
Result rename(Database *db, const QString &projectGuid, const QString &guid,
              const QString &name);

/// Deletes a folder. With `keepContents` (the default) everything inside —
/// child folders and filed rows alike — moves up to the deleted folder's
/// parent, so nothing can be lost by tidying up. Without it the folder's rows
/// LEAVE THE PROJECT (assetdelete::removeFromProject — the project's pin, never
/// the library row) and its subtree of folders goes with it. Refuses the root,
/// a system folder and an unknown folder.
Result remove(Database *db, const QString &projectGuid, const QString &guid,
              bool keepContents = true);

/// WOULD IT BE ACCEPTED, AND WOULD IT MOVE ANYTHING? The same rules, the same
/// words and the same count as `moveTo`, with nothing written — for the same
/// reason as `judgeCreate`, plus one of its own: a move whose rows are all
/// already in that folder moves nothing, and THAT must not become an undo step
/// either.
Result judgeMove(Database *db, const QString &projectGuid, const QStringList &guids,
                 const QString &folderGuid);

/// Files `guids` in `folderGuid` (empty, or the project's own guid, = the
/// root). One row or many; a folder guid moves the FOLDER (a move into its own
/// subtree, and any move of a system folder, is refused). Refuses an unknown
/// row, an import MEMBER (its parent is another asset — it is part of that
/// asset, not in a folder), a row this project neither owns nor pins, and an
/// unknown target folder. `moved` counts the rows that were not already there.
Result moveTo(Database *db, const QString &projectGuid, const QStringList &guids,
              const QString &folderGuid);

/// Where this project files `guid` today — a folder guid, or empty for the
/// root. Reads the pin's column for a pinned library row and `assets.parent`
/// for a row the project owns.
QString folderOf(Database *db, const QString &projectGuid, const QString &guid);

/// Files ONE row, with no rules applied (they live in `moveTo`; this is what
/// the undo command replays). A `folder` that no longer exists means the ROOT:
/// an undo replays a filing the user may have deleted the folder of, and an
/// owned row left pointing at a dead guid is listed by nothing at all. False
/// when nothing could be written.
bool file(Database *db, const QString &projectGuid, const QString &guid,
          const QString &folder);

/// "This project's listing changed" — ProjectMembership, the one signal both
/// drawers repopulate from.
void announce(const QString &projectGuid);

}   // namespace projectfolders

#endif   // PROJECTFOLDERS_H
