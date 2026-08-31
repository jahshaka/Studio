/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef RAWEXPORTER_H
#define RAWEXPORTER_H

// RawExporter (ASSET_PIPELINE_SPEC §3.3) — the missing "give me my file back":
// copies assets' stored files to a chosen directory with their original names
// and writes a manifest v2 (jah.manifest.json) describing what was exported.
// The simplest unified-export proof: catalog metadata comes from the caller,
// bytes come through the ExportContentSource seam, the manifest is the shared
// format — no DB coupling, no UI, headless-safe.
//
// Name collisions across assets dedupe as "name 1.ext" (the project-folder
// convention) — unless the colliding files have the same oid, in which case
// the file is written once and both manifest entries carry the shared name.
// Assets with zero stored files still get manifest entries (DB-only rows are
// valid; identity survives absence).

#include <QString>
#include <QStringList>
#include <QVector>

#include "export/exportmanifest.h"

class ExportContentSource;

class RawExporter
{
public:
    /// Catalog facts about one asset to export — built by the caller (the
    /// assets.exportRaw verb reads them from Database); keeps this class a
    /// pure storage consumer.
    struct AssetInfo
    {
        QString guid;
        QString name;
        QString type;              // assets.* vocabulary ("object", "texture", …)
        int typeId = -1;           // raw ModelTypes value
        QStringList dependencies;  // outgoing edges (guids)
    };

    struct Result
    {
        bool ok = false;
        QString error;
        QStringList warnings;
        QString dir;               // the export directory (created if missing)
        QString manifestPath;      // <dir>/jah.manifest.json
        QStringList exportedFiles; // file names written (manifest excluded)
        int assetCount = 0;        // manifest entries written
        qint64 totalBytes = 0;     // bytes copied
    };

    /// Exports every asset in `assets` (files via `source`) into `destDir`.
    /// `kind` labels the manifest ("raw" by default). With `copyFiles` false,
    /// only the manifest is written — file entries describe the stored bytes
    /// (name/size/oid) without materializing them (project.exportManifest).
    /// Never throws; failures land in Result::error, per-file problems in
    /// warnings.
    static Result exportAssets(const QVector<AssetInfo> &assets,
                               ExportContentSource &source,
                               const QString &destDir,
                               const QString &kind = QStringLiteral("raw"),
                               bool copyFiles = true);
};

#endif // RAWEXPORTER_H
