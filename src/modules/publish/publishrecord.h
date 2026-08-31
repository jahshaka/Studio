/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PUBLISHRECORD_H
#define PUBLISHRECORD_H

// PublishRecord — the per-project memory of the last web publish. A publish
// is LINKED to its project (owner decision): one publish per project at the
// stable `<project>/exports/web` path, Process always (re)generates it there,
// and the Publish page reflects it whenever the project opens.
//
// Persisted as `.jah-publish.json` in the PROJECT folder — project-scoped
// (survives app restarts and travels with the project), and deliberately
// OUTSIDE the export dir so the record survives the user deleting the export
// (that is the Missing state: "previous publish missing — Process to
// regenerate"). Projects published before this record existed are backfilled
// from the export's index.html mtime.

#include <QDateTime>
#include <QString>

class PublishRecord
{
public:
    enum class State {
        None,    // never published (no record, no export on disk)
        Present, // record + the export's index.html exists
        Missing  // publish on record but the directory is gone from disk
    };

    QString dir;     // the export directory of the last publish
    QDateTime when;  // when it was published

    bool isValid() const { return !dir.isEmpty() && when.isValid(); }
    QString indexHtml() const;   // <dir>/index.html (path only, no existence check)
    State state() const;         // existence-checked, safe on any value

    // Record file for a project folder (empty folder -> empty path).
    static QString filePath(const QString &projectFolder);

    // Load the project's record. When no record exists but a finished export
    // sits at `conventionalDir` (pre-record projects), synthesizes one from
    // the index.html mtime — without writing it back.
    static PublishRecord load(const QString &projectFolder,
                              const QString &conventionalDir = QString());

    // Persist `dir` + `when` as the project's last publish.
    static bool save(const QString &projectFolder, const QString &dir,
                     const QDateTime &when = QDateTime::currentDateTime());
};

#endif // PUBLISHRECORD_H
