/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EXPORTMANIFEST_H
#define EXPORTMANIFEST_H

// Export manifest v2 (ASSET_PIPELINE_SPEC §3.3, phase-5 front half).
//
// One manifest format for every unified export (.jaf, raw file exports,
// project archives). v1 is the legacy .jaf `.manifest`: a single-word text
// line naming the payload type ("object", "texture", "material", "shader",
// "sky", "particle_system", "bundle"). v2 is real JSON:
//
//   {
//     "format": "jah-export-manifest",
//     "version": 2,
//     "kind": "object" | "texture" | ... | "bundle" | "project" | "raw",
//     "generator": "Jahshaka",
//     "created": "2026-08-31T12:00:00Z",
//     "assets": [ {
//        "guid": "...", "name": "lotus.glb",
//        "type": "object",              // assets.* verb type vocabulary
//        "typeId": 5,                   // raw ModelTypes int (lossless)
//        "files": [ { "role": "source", "name": "lotus.glb",
//                     "size": 24567890, "oid": "<sha256 hex>" } ],
//        "dependencies": [ "guid2", ... ]   // OUTGOING edges (what it needs)
//     } ]
//   }
//
// The CAS seam (Lane A phase 2): `oid` is the SHA-256 hex of the file's bytes
// — exactly the content-addressed store's object id. Sources that cannot hash
// omit the field (readers treat an absent/empty oid as "unknown", never an
// error); the resolver-backed content source fills it from the `files` table
// and the values are identical to hashes computed here, so manifests are
// stable across the front-half → final-half swap.
//
// Readers keep accepting v1: fromBytes() maps a single-word manifest to
// {version: 1, kind: <word>, assets: []}.

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace exportformat {

struct ManifestFile
{
    QString role;      // "source" | "texture" | "sidecar" | ...
    QString name;      // file name inside the payload (original name, deduped)
    qint64 size = -1;  // bytes; -1 = unknown
    QString oid;       // sha256 hex of the bytes; empty = unknown (CAS seam)
};

struct ManifestAsset
{
    QString guid;
    QString name;
    QString type;            // "object", "texture", ... (assets.* vocabulary)
    int typeId = -1;         // raw ModelTypes value
    QVector<ManifestFile> files;
    QStringList dependencies;   // guids this asset depends on (outgoing edges)
};

struct ExportManifest
{
    int version = 2;         // 1 = legacy single-word manifest
    QString kind;            // payload type; v1 words pass through unchanged
    QString generator;       // informational
    QString created;         // ISO 8601 UTC, informational
    QVector<ManifestAsset> assets;

    QJsonObject toJson() const;
    QByteArray toBytes() const;                 // pretty JSON, trailing newline
    bool write(const QString &path, QString *error = nullptr) const;

    /// Accepts v2 JSON and v1 single-word manifests. On failure returns a
    /// manifest with version 0 and sets *error.
    static ExportManifest fromBytes(const QByteArray &bytes, QString *error = nullptr);
    static ExportManifest fromFile(const QString &path, QString *error = nullptr);

    bool isValid() const { return version > 0; }
};

/// The default manifest file name for directory-shaped exports (raw export,
/// project archives). The .jaf zip keeps its historical entry name ".manifest".
inline QString manifestFileName() { return QStringLiteral("jah.manifest.json"); }

} // namespace exportformat

#endif // EXPORTMANIFEST_H
