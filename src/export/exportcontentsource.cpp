/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "export/exportcontentsource.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>

#include "data/database/dbqt.h"
#include "services/assetstorepaths.h"

CasContentSource::CasContentSource(db::Connection *connection, const QString &storeRoot,
                                   const QString &projectGuid)
    : connection(connection), root(storeRoot), project(projectGuid)
{
}

QVector<ExportContentSource::Entry> CasContentSource::filesForAsset(const QString &guid,
                                                                    const QString &nameHint)
{
    Q_UNUSED(nameHint);   // the catalog IS the name authority
    QVector<Entry> entries;
    if (guid.isEmpty() || !connection) return entries;

    // THE MESH BAKE IS NOT EXPORTED (MESH_BAKE_SPEC phase 1, design call).
    // It is derived data keyed on the BUILD that produced it: an archive
    // carrying one would ship megabytes that the importing installation is
    // likely to reject as stale on sight, and the .jaf ingest has no way to
    // preserve a role anyway (it re-derives 'source' vs 'file' from the file
    // name). An imported project therefore arrives with sources only and
    // re-bakes lazily on its first open — the same path every pre-bake
    // library takes. (Phase 2's "runtime only, no sources" mode is where a
    // bake becomes archive PAYLOAD, and it will carry its own marker.)
    //
    // ONE STATEMENT PER ASSET (D11-LIBRARY-SCALE §3.6): the project's PIN on
    // the source (copy semantics: what the project renders with is what
    // travels) and the pinned object's size/extension ride the files query as
    // LEFT JOINs — they were two more statements per asset. No project, no
    // pin row: the joins answer NULL.
    db::Statement files(*connection,
                        "SELECT AF.role, AF.name, AF.oid, F.size, F.ext, PA.oid_pin, PF.oid, PF.size, PF.ext "
                        "FROM asset_files AF "
                        "LEFT JOIN files F ON AF.oid = F.oid "
                        "LEFT JOIN project_assets PA ON PA.asset_guid = AF.asset_guid AND PA.project_guid = ? "
                        "LEFT JOIN files PF ON PF.oid = PA.oid_pin "
                        "WHERE AF.asset_guid = ? AND AF.role <> 'bake' "
                        "ORDER BY AF.role, AF.name");
    dbqt::bindText(files, 1, project);
    dbqt::bindText(files, 2, guid);
    ++sStatements;
    db::Error error;
    while (files.step(&error)) {
        Entry e;
        e.role = dbqt::text(files, 0);
        e.name = dbqt::text(files, 1);
        e.oid = dbqt::text(files, 2);
        e.size = files.int64(3);
        QString ext = dbqt::text(files, 4);

        const QString pin = dbqt::text(files, 5);
        if (e.role == QStringLiteral("source") && !pin.isEmpty() && pin != e.oid
            && !files.isNull(6)) {
            e.oid = pin;
            e.size = files.int64(7);
            ext = dbqt::text(files, 8);
        }

        e.path = AssetStorePaths::objectPathIn(root, e.oid, ext);
        if (!QFileInfo::exists(e.path)) continue;   // offline/purged object
        entries.append(e);
    }
    if (error.code) qWarning("export: the catalog's files of %s: %s", qUtf8Printable(guid), qUtf8Printable(dbqt::describe(error)));
    return entries;
}

int CasContentSource::sStatements = 0;
