/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETCLOSURE_H
#define ASSETCLOSURE_H

// AssetClosure — what has to TRAVEL with a selection (CLIPBOARD_SPEC P1).
//
// Two halves, and the split is the whole point:
//
//   forNodes()  — the KEY-AWARE walk over node objects (src/io/assetrefs.h)
//                 that says which asset guids a selection names, then the
//                 catalog's dependency edges from each of those, transitively.
//                 It replaced AssetHelper::getChildGuids (deleted), which
//                 read NODE guids as asset guids — an identity the paste path
//                 breaks by design (regenerateGuids) — so a pasted model's
//                 export carried NO dependencies at all (spec §6.6).
//
//   describe()  — turns guids into manifest-v2 asset entries (name, type,
//                 dependencies, files by oid — CasContentSource, project-pin
//                 aware) and inlines the BYTES of everything that fits the
//                 budget. Above the budget an entry keeps its oid, which is
//                 enough to find the same content in any store that has it.
//
// The owner's portability requirement ("an exported project must be
// self-contained for a user with an empty library") is this same closure with
// an unbounded budget — one walker, two size policies. The archiver and the
// raw exporter still run their own sweeps; adopting this is recorded as the
// follow-on it is, not smuggled into a clipboard lane.

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>


#include "io/clipboardformat.h"

class Database;

namespace assetclosure {

struct Options
{
    /// Total inline budget across the whole payload, in bytes. Files are
    /// inlined in walk order while the budget lasts; the rest travel as oids.
    /// 0 = never inline (pure Unreal: references only).
    qint64 inlineLimitBytes = 4 * 1024 * 1024;
    /// The project whose PIN decides which version of an asset travels — copy
    /// semantics: what the source project renders with is what is copied.
    QString projectGuid;
    /// The store root the bytes are read from (explicit: exporters and
    /// services never derive storage paths).
    QString storeRoot;
    /// Carry the catalog row's own JSON columns (`asset` blob, `properties`).
    /// On for `asset` items (a library tile pasted into another library has to
    /// arrive as a usable row), off for the closure riding a `node` item.
    bool includeRowBlobs = false;
};

/// The asset guids `nodeObjects` reference, plus every guid those depend on,
/// transitively through the catalog. Reserved builtin guids are excluded (they
/// exist in every install and their ids collide across types).
QStringList forNodes(const QVector<QJsonObject> &nodeObjects, Database *db);

/// The catalog closure of `seeds` themselves (an `asset` item's tiles).
QStringList expand(const QStringList &seeds, Database *db);

/// One file whose bytes are OWED to an entry: `describe` decided it travels
/// inline (it fits the budget) but, asked to defer, did not read it. The
/// read is file I/O only — no catalog, no store lookup — so it is the half of
/// an export that runs on a worker thread (EXPORT-THREAD-1).
struct DeferredRead
{
    QString guid;       ///< the ClipAsset the bytes belong to
    int fileIndex = -1; ///< its index in that asset's `files`
    QString path;       ///< the store object to read
};

/// Describe `guids` as envelope entries, inlining bytes while the budget
/// lasts. `inlinedOut`/`referencedOut` (optional) count the two outcomes.
///
/// `deferredOut`, when given, turns the inlining into a LIST: every file the
/// budget admits is appended there instead of being read, and its
/// `inlineData` stays empty until the share file's writer reads it
/// (services/bundlewriter.cpp). The catalog and
/// store lookups are the caller's thread's (the UI thread owns the default
/// connection); the bytes are not.
QMap<QString, clipboardformat::ClipAsset> describe(const QStringList &guids, Database *db,
                                                   const Options &options,
                                                   int *inlinedOut = nullptr,
                                                   int *referencedOut = nullptr,
                                                   QVector<DeferredRead> *deferredOut = nullptr);

} // namespace assetclosure

#endif // ASSETCLOSURE_H
