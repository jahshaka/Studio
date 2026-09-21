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
// KNOWN GAP, stated rather than hidden: a project ARCHIVE does not carry the
// pin's folder (services/projectarchiver.cpp exports rows and re-pins), so a
// pinned library asset comes back at the root of the imported project. A row
// the project OWNS keeps its folder, because that rides `assets.parent` in the
// exported asset row.
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
/// rows the project owns plus pins filed there, not a recursive total and not
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

/// A FOLDER THE EDITOR OWNS BY NAME. `Database::ensureFolder` resolves these
/// by NAME inside the project — "Systems" holds a row per particle emitter,
/// "Presets" a row per applied material preset — so renaming or deleting one,
/// or minting a user folder that takes the name, silently breaks the writer
/// that looks it up. Refused at this door, in all three directions.
bool isSystemFolder(const QString &name);

/// The project's folders: every one of them, or (with `parent` set) just that
/// parent's children. The project's own guid names the ROOT.
QVector<Info> list(Database *db, const QString &projectGuid,
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

/// Files `guids` in `folderGuid` (empty, or the project's own guid, = the
/// root). One row or many; a folder guid moves the FOLDER (a move into its own
/// subtree is refused). Refuses an unknown row, an import MEMBER (its parent is
/// another asset — it is part of that asset, not in a folder), a row this
/// project neither owns nor pins, and an unknown target folder. `moved` counts
/// the rows that were not already there.
Result moveTo(Database *db, const QString &projectGuid, const QStringList &guids,
              const QString &folderGuid);

/// Where this project files `guid` today — a folder guid, or empty for the
/// root. Reads the pin's column for a pinned library row and `assets.parent`
/// for a row the project owns.
QString folderOf(Database *db, const QString &projectGuid, const QString &guid);

/// Files ONE row, with no rules applied (they live in `moveTo`; this is what
/// the undo command replays). False when nothing could be written.
bool file(Database *db, const QString &projectGuid, const QString &guid,
          const QString &folder);

/// "This project's listing changed" — ProjectMembership, the one signal both
/// drawers repopulate from.
void announce(const QString &projectGuid);

}   // namespace projectfolders

#endif   // PROJECTFOLDERS_H
