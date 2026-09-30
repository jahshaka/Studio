/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EXPORTCONTENTSOURCE_H
#define EXPORTCONTENTSOURCE_H

// ExportContentSource — the seam between exporters and asset storage
// (ASSET_PIPELINE_SPEC §3.3 "files are gathered by oid through the resolver").
//
// The one production source is CasContentSource: entries come from the
// `asset_files`/`files` tables and the store's objects/. `oid` is sha256 hex of
// the bytes — the CAS object id. A DB-only row yields ZERO files, not an error.
//
// The store root is always passed in EXPLICITLY: exporters must not derive
// storage paths (that authority is AssetStorePaths).

#include <QString>
#include <QVector>

class ExportContentSource
{
public:
    virtual ~ExportContentSource() = default;

    struct Entry
    {
        QString role;   // the asset_files role ("source", "texture", …)
        QString name;   // file name (display + export naming)
        QString path;   // absolute path to readable bytes
        qint64 size = -1;
        QString oid;    // sha256 hex; empty = unknown
    };

    /// Every stored file belonging to `guid`. Empty = the asset has no stored
    /// bytes (a DB-only row) — callers treat that as a valid, file-less asset.
    /// `nameHint` is the catalog's file name for the asset (the CAS source
    /// ignores it).
    virtual QVector<Entry> filesForAsset(const QString &guid,
                                         const QString &nameHint = QString()) = 0;
};

/// The resolver-backed source (final half, phase 4): entries come from the
/// `asset_files`/`files` catalog and point into objects/. When a project
/// guid is given, the SOURCE role serves the project's PINNED content
/// (spec §3.1.5 — exports materialize the bytes the project renders with).
class CasContentSource : public ExportContentSource
{
public:
    explicit CasContentSource(const QString &storeRoot,
                              const QString &projectGuid = QString());

    QVector<Entry> filesForAsset(const QString &guid,
                                 const QString &nameHint = QString()) override;

    /// Catalog statements filesForAsset has run this process — ONE per asset
    /// (the archive.closure suite's count).
    static int statements() { return sStatements; }

private:
    QString root;
    QString project;
    static int sStatements;
};

#endif // EXPORTCONTENTSOURCE_H
