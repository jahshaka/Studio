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
#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>

#include "services/assetcas.h"

class QTemporaryDir;

struct ImportRequest
{
    /// WHO ASKED FOR THIS IMPORT (IMPORT-INTENT-1, MATERIAL_BUNDLE_SPEC V-2's
    /// F14). A property of the REQUEST, never of the pipeline: the same file,
    /// the same importer, the same bytes — the difference is whose gesture it
    /// was, and that decides what happens when the import lands on a row the
    /// library already has.
    ///
    ///   User      — a person asked: the Assets page's import button and its
    ///               drop pad, a drop on the editor's project panel,
    ///               assets.import / assets.importFile / assets.importAndPlace,
    ///               the avatar module's character import. THE DEFAULT, because
    ///               every door a person can reach is one of these, and the
    ///               honest answer for a new door is "the user asked".
    ///   Material  — a MATERIAL asked: the texture picker's import
    ///               (materials.addTexture with a path, through
    ///               ShippedAssets::importTexture), a graph texture node's
    ///               picker, the first-run preset seed's maps. The picture is
    ///               coming in INSIDE a material, so it is stamped as that
    ///               material's member and folds into the bundle's tile.
    ///
    /// The stamp is an ORIGIN and the user's intent outranks it: an import the
    /// USER asked for that lands on an existing stamped row takes the stamp
    /// off (services/memberstamp.h), so the picture is their tile from then on
    /// — and the material keeps it as a member, because membership is the
    /// definition's edges and never the stamp. An import a MATERIAL asked for
    /// never clears one.
    enum class Intent { User, Material };

    QString sourcePath;
    int typeHint = -1;          // ModelTypes value; -1 = sniff from the file
    /// THE ROW'S GUID, WHEN THE CALLER OWNS IT (ATOM P2). Empty for every import
    /// a person makes: the pipeline mints one. Non-empty ONLY for the shipped
    /// SEEDS whose guid is reserved and persisted elsewhere — the primitives
    /// (src/data/primitives.h: a favourite, a tile's drop payload and
    /// `assets.builtins` all name it), which must therefore be the SAME row in
    /// every library rather than whatever the minter happened to produce.
    /// The store is content-addressed at the OBJECT level (a sha256), so the
    /// ROW's identity was always free to be chosen; nothing else about the
    /// import changes.
    QString reservedGuid;
    int drawerId = -1;          // > 0: file the imported asset in this drawer
    QString projectGuid;        // stamps created rows (may be empty)
    QJsonObject settings;       // recorded per-import; part of the determinism key
    bool wantViewerThumbnail = false;  // UI refreshes the thumbnail after preview
    Intent intent = Intent::User;      // see above — every user-facing door
};

/// Is this path a MODEL file — the one kind the import dialog asks about?
/// Media never prompts, and a .jaf archive carries assets that were already
/// imported with their own settings.
bool isModelImportPath(const QString &path);

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
    bool ok() const { return error.isEmpty() && !assetGuid.isEmpty(); }
};

/// One content file the import stores: ingested CAS-first under `forGuid`
/// with an explicit role; `name` is display/export naming only.
struct StagedFile
{
    QString path;               // absolute, readable now (source or staging)
    QString forGuid;            // owning asset row
    QString role;               // "source" | "texture" | "sidecar" | "file" | "bake"
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
    QString jafKind;
    StagedJaf jaf;
    QStringList warnings;
    std::function<void()> registerSession;   // AssetManager adds; runs after commit

    /// The COMPLETE import-settings record this import actually applied
    /// (irisgl/import/importsettings.h), when the importer has one. Empty for
    /// every importer that takes no settings (media, .jaf). The spine records
    /// it in the determinism record instead of the caller's partial request,
    /// so `assets.importSettings` and the re-bake lookup read one shape.
    QJsonObject appliedSettings;

    /// sha256 of the ORIGINAL source file, when the importer already computed
    /// it (MeshImporter needs it to key the mesh bake). prepare() reuses this
    /// for the determinism record instead of hashing the same file twice.
    QString sourceOid;

    /// Content hashes precomputed off the DB thread (path → sha256 oid).
    /// AssetImportService::prepare fills this on the worker so the UI-thread
    /// commit never re-hashes big files; ingestFile trusts a present entry.
    QMap<QString, QString> fileOids;

    /// THE BYTES, ALREADY IN THE STORE AND ALREADY DURABLE (FSYNC-2).
    /// prepare() runs AssetCas::stage + flushStaged over every file above on
    /// the worker, so the commit has nothing left to copy and nothing left to
    /// flush — it renames each staged temp into place and writes the rows
    /// (AssetCas::commitStaged). Keyed by StagedFile::path through
    /// stagedEntryFor(); a file with no entry here (a hand-built plan, a
    /// staging failure) falls back to the synchronous AssetCas::ingestFile, so
    /// this is an optimisation the commit never depends on.
    QVector<AssetCas::Staged> stagedBytes;
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
