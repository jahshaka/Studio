/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef IMPORTTYPES_H
#define IMPORTTYPES_H

// The ONE import pipeline's shared types (ASSET_PIPELINE_SPEC §3.2, phase 3).
//
//   source → sniff → validate → convert (staging) → store (CAS) → register
//
// Per-type importers produce a StagedAsset — a complete, side-effect-free
// description of everything the import creates: content files (by absolute
// path, wherever they live — the source, a staging dir), catalog rows,
// dependency edges, and the session registration. The SPINE owns committing
// it (one transaction, CAS-first ingest, sidecars, rollback), so importers
// cannot diverge on storage behavior — the defect class that produced five
// parallel import implementations.

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>

class QTemporaryDir;

#include "irisgl/irisglfwd.h"

struct ImportRequest
{
    QString sourcePath;
    int typeHint = -1;          // ModelTypes value; -1 = sniff from the file
    int drawerId = -1;          // > 0: file the imported asset in this drawer
    QString projectGuid;        // stamps created rows (may be empty)
    QJsonObject settings;       // recorded per-import; part of the determinism key
    bool wantViewerThumbnail = false;  // UI refreshes the thumbnail after preview
};

struct ImportResult
{
    QString assetGuid;          // the library row (Object/Texture/Music/Video/…)
    QString meshGuid;           // mesh member row, when applicable
    QString error;              // non-empty = failed
    QStringList warnings;
    QStringList objectOids;     // every CAS object this import wrote or reused
    QString jafKind;            // .jaf imports: the manifest kind ("object", …)
    QMap<QString, QString> guidMap;   // .jaf imports: archive guid → new guid
    QJsonObject metadata;       // the describe-stage block recorded on the row
    iris::SceneNodePtr node;    // mesh imports: the guid-rewritten fragment
    bool ok() const { return error.isEmpty() && !assetGuid.isEmpty(); }
};

/// One content file the import stores: ingested CAS-first under `forGuid`
/// with an explicit role; `name` is display/export naming only.
struct StagedFile
{
    QString path;               // absolute, readable now (source or staging)
    QString forGuid;            // owning asset row
    QString role;               // "source" | "texture" | "sidecar" | "file"
    QString name;               // display name (defaults to the file name)
};

/// One catalog row the import creates (Database::createAssetEntry shape).
struct StagedRow
{
    QString guid;
    QString name;
    int type = 0;               // ModelTypes value
    QString parent;             // parent guidchain ("" = root)
    QByteArray thumbnail;       // PNG bytes (may be empty)
    QByteArray properties;      // JSON (may be empty)
    QByteArray tags;
    QByteArray asset;           // node/definition JSON blob (may be empty)
    int viewFilter = 1;         // AssetViewFilter value (Editor=1, AssetsView=2)
};

struct StagedDep
{
    int dependerType = 0;
    int dependeeType = 0;
    QString depender;
    QString dependee;
    QString projectGuid;
};

/// .jaf archives carry their own row set (asset.db) and payload dirs; the
/// spine commits them through Database::importAsset/importAssetBundle and
/// ingests the payload CAS-first. kind empty = not a .jaf import.
struct StagedJaf
{
    QString kind;               // "object" | "texture" | … | "bundle"
    QString dbPath;             // extracted asset.db
    QString assetsDir;          // extracted assets/ payload
    QStringList bundleLines;    // bundle manifests: the member guid list
};

/// The importer's complete plan; the spine commits it atomically.
struct StagedAsset
{
    QString mainGuid;                    // the row ImportResult::assetGuid reports
    QString meshGuid;
    QVector<StagedFile> files;
    QVector<StagedRow> rows;
    QVector<StagedDep> deps;
    QJsonObject metadata;                // describe-stage block ("metadata" property)
    QJsonObject importRecord;            // determinism record ("import" property)
    iris::SceneNodePtr node;
    QString jafKind;
    StagedJaf jaf;
    QStringList warnings;
    std::function<void()> registerSession;   // AssetManager adds; runs after commit

    /// Content hashes precomputed off the DB thread (path → sha256 oid).
    /// AssetImportService::prepare fills this on the worker so the UI-thread
    /// commit never re-hashes big files; ingestFile trusts a present entry.
    QMap<QString, QString> fileOids;
};

/// The output of AssetImportService::prepare — everything the CPU-heavy half
/// of the pipeline produced, ready for the DB-thread commit. Carries NO live
/// Qt GUI objects (thumbnails are PNG byte arrays, images decode to QImage
/// before this point), so it may cross threads freely. The staging dir that
/// backs StagedFile paths is owned here and must outlive commit().
struct PreparedImport
{
    ImportRequest request;
    StagedAsset staged;
    ImportResult result;                     // error/warnings from the prepare half
    std::shared_ptr<QTemporaryDir> staging;  // keeps staged file paths alive
    bool ok() const { return result.error.isEmpty(); }
};

/// Progress callback: stage name + current/total within the stage
/// (total 0 = indeterminate). Return false to request cancellation.
using ImportProgressFn = std::function<bool(const QString &stage, int done, int total)>;

#endif // IMPORTTYPES_H
