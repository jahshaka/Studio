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
// Front half (this file): LegacyStoreContentSource walks the per-guid
// AssetStore folder — <storeRoot>/<guid>/* — which is today's layout. A
// missing folder yields ZERO files, not an error (preflight §3.1: most Editor
// rows have no store folder; identity survives absence).
//
// Final half (after Lane A's phase 2 CAS lands): a resolver-backed source
// implements the same interface over the `asset_files`/`files` tables and
// objects/ paths. `oid` here is sha256 hex of the bytes — the CAS object id —
// so entries produced by either source are identical; exporters and manifests
// never notice the swap.
//
// The store root is always passed in EXPLICITLY: exporters must not derive
// storage paths (that authority is Lane A's AssetStorePaths). Verb-level
// callers pass AssetMetadata::storeRootPath() — the one canonical helper —
// which the lead reroutes through AssetStorePaths at merge.

#include <QString>
#include <QVector>

class ExportContentSource
{
public:
    virtual ~ExportContentSource() = default;

    struct Entry
    {
        QString role;   // "source" for legacy store files (roles refine under CAS)
        QString name;   // file name (display + export naming)
        QString path;   // absolute path to readable bytes
        qint64 size = -1;
        QString oid;    // sha256 hex; empty = unknown
    };

    /// Every stored file belonging to `guid`. Empty = the asset has no stored
    /// bytes (a DB-only row) — callers treat that as a valid, file-less asset.
    /// `nameHint` is the catalog's file name for the asset — legacy resolution
    /// is name-keyed (spec §1.2), so sources that fall back to a flat folder
    /// need it; the CAS source will ignore it.
    virtual QVector<Entry> filesForAsset(const QString &guid,
                                         const QString &nameHint = QString()) = 0;
};

/// Today's per-guid-folder store layout (front half).
class LegacyStoreContentSource : public ExportContentSource
{
public:
    /// `storeRoot` = the AssetStore root directory (explicit, see header).
    /// `computeHashes` fills Entry::oid with sha256 hex (streamed; the owner's
    /// whole store is ~222 MB, so hashing is cheap and manifests get stable
    /// content ids from day one).
    /// `fallbackDir`: when the guid has no store folder, look for `nameHint`
    /// in this flat directory — the project-folder resolution project rows use
    /// today (folder + name IS the current join; this encoding of it dies with
    /// the final-half resolver swap).
    explicit LegacyStoreContentSource(const QString &storeRoot,
                                      bool computeHashes = true,
                                      const QString &fallbackDir = QString());

    QVector<Entry> filesForAsset(const QString &guid,
                                 const QString &nameHint = QString()) override;

private:
    QString root;
    bool hashFiles;
    QString fallback;
};

#endif // EXPORTCONTENTSOURCE_H
