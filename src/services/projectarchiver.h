/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECTARCHIVER_H
#define PROJECTARCHIVER_H

// Project archives, pin-world edition (ASSET_PIPELINE_SPEC §3.1.5/§3.3,
// phase 4). A project archive (.zip) is self-contained:
//
//   jah.manifest.json      manifest v2, kind "project": per-asset file lists
//                          {role, name, oid} — the pinned content ids
//   <projectGuid>.db       the catalog snapshot (projects/assets/deps rows —
//                          the same blob db Database::createExportScene has
//                          always written, now pin-aware)
//   objects/<oid>.<ext>    the pinned bytes, materialized from the CAS
//
// Export materializes pinned oids (a reference-based project leaves the
// machine self-contained); import ingests the objects CAS-first, imports the
// catalog rows, and writes fresh pins — NO flat project-folder file copies
// exist any more, on either side. The bundled sample scenes ship in exactly
// this format.

#include <QString>
#include <QStringList>

class Database;
class Project;

class ProjectArchiver
{
public:
    struct Result
    {
        QString error;
        QString path;          // export: the archive; import: extracted temp
        QString projectGuid;   // import: the NEW project guid
        QString worldName;     // import: the project's display name
        int assets = 0;
        int objects = 0;
        bool ok() const { return error.isEmpty(); }
    };

    /// Export the open project to a self-contained archive at destZipPath.
    static Result exportArchive(Database *db, Project *project, const QString &destZipPath);

    /// Import an archive as a NEW project (fresh guid; rows + objects + pins).
    /// Does not open it — callers decide (UI opens or adds a desktop tile).
    static Result importArchive(Database *db, const QString &zipPath);
};

#endif // PROJECTARCHIVER_H
