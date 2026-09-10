/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/apppaths.h"
#include "scripting/modules/assetsapi.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QPixmap>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <cmath>

#include "scripting/modules/moduleshared.h"
#include "export/exportcontentsource.h"
#include "export/rawexporter.h"
#include "services/animationfile.h"
#include "services/assetcas.h"
#include "services/assetgc.h"
#include "services/assetservice.h"
#include "services/assetmigration.h"
#include "services/assetstore.h"
#include "services/assettags.h"
#include "services/assetstorepaths.h"
#include "services/meshbakestore.h"
#include "data/constants.h"
#include "data/settingsmanager.h"
#include "services/assethelper.h"
#include "services/projectassets.h"
#include "services/import/assetimportservice.h"
#include "services/assetmetadata.h"
#include "services/fitsize.h"
#include "services/thumbnailmanager.h"
#include "services/videoutils.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "bridge/enginethumbnailrenderer.h"
#include "viewport/ieditorviewport.h"
#include "bridge/enginehost.h"
#include "io/assetmanager.h"
#include "io/materialreader.h"
#include "io/scenereader.h"
#include "shell/mainwindow.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "services/assetdelete.h"

using namespace scriptmod;

namespace {

QString typeName(int type)
{
    return scriptmod::assetTypeName(type);   // shared vocabulary (moduleshared.h)
}

int typeFromName(const QString &name)
{
    const QString n = name.trimmed().toLower();
    if (n == "material") return static_cast<int>(ModelTypes::Material);
    if (n == "texture") return static_cast<int>(ModelTypes::Texture);
    if (n == "video") return static_cast<int>(ModelTypes::Video);
    if (n == "sky") return static_cast<int>(ModelTypes::Sky);
    if (n == "object" || n == "model") return static_cast<int>(ModelTypes::Object);
    if (n == "mesh") return static_cast<int>(ModelTypes::Mesh);
    if (n == "music") return static_cast<int>(ModelTypes::Music);
    if (n == "shader") return static_cast<int>(ModelTypes::Shader);
    if (n == "file") return static_cast<int>(ModelTypes::File);
    if (n == "particles") return static_cast<int>(ModelTypes::ParticleSystem);
    if (n == "lightprofile" || n == "ies") return static_cast<int>(ModelTypes::LightProfile);
    if (n == "avatar") return static_cast<int>(ModelTypes::Avatar);
    if (n == "animation" || n == "clip") return static_cast<int>(ModelTypes::Animation);
    return -1;
}

// The asset's stored bytes, by guid — the CAS resolver, never the retired
// <root>/<guid>/ view (deep audit 2026-09, area 6). Falls back to the legacy
// folder itself for stores that still have one (AssetCas::resolveSource).
QString storeFileFor(const QString &guid)
{
    return AssetCas::resolveSource(QSqlDatabase::database(), AssetStorePaths::root(), guid);
}

} // namespace

QVector<VerbInfo> AssetsApi::verbs() const
{
    return {
        { "list", "assets.list({scope: 'store'|'project'|'session', type, query, tag, drawer, rigged, limit}) -> [{guid, name, type, drawer}]",
          "Store assets (default) or the open project's assets, optionally filtered by type name. A type-filtered project listing sweeps every folder (materials registered under Presets/ included); unfiltered it lists the root folder. drawer is the containing drawer's id (0 = Uncategorized). Scope 'session' lists the live session registrations (the AssetManager entries project open + add-to-project hydrate — what the editor's drag-drop paths look up); drawer is absent there. "
          "query is a case-insensitive substring match on the asset NAME; tag keeps only rows carrying that TAG (case-insensitive, exact — assets.setTags writes them, and scope 'session' has none, so a tag filter there is refused); drawer restricts the listing to one drawer id (0 = Uncategorized, refused for scope 'session', which carries no drawer); rigged: true keeps only MODEL rows whose metadata says the file carries a skeleton (the candidates avatar.createAsset accepts — refused for scope 'session', which has no metadata); limit caps how many rows come back (<= 0 means no cap). Filters apply in that order — type, then drawer, then query, then tag, then rigged — and limit last, so a limited listing is the first N of the filtered set, not a sample of it. rigged is the expensive one on a library that predates the rig metadata (it backfills the block once per row it reaches), which is why it is applied last.",
          Needs::Document },
        { "metadata", "assets.metadata(guid) -> {guid, name, type, tags, imported, kind, format, fileSize, ...}",
          "Rich per-type metadata for a store asset. Models: vertices, triangles, meshes, materials, textures, plus the RIG block — hasSkeleton, bones, boneNames, nodeNames, rigId (a stable hash of the sorted bone names: two exports of one skeleton share it) and animations [{name, length in seconds, channels, boneChannels}]; images: width, height; audio (wav): duration (ms), sampleRate, channels, bitsPerSample; video: duration (ms), width, height, frameRate, videoCodec; every kind: format + fileSize. Computed at import since the metadata feature landed; for older rows the first call computes it from the store files and persists it (lazy backfill). "
          "`tags` is the row's tag list (assets.setTags writes it, assets.list({tag}) filters on it) — always present, an empty array for an untagged asset. "
          "MODELS also carry the FIT-TO-SIZE block (services/fitsize.h): `extent` {x,y,z} — the model's axis-aligned size in METRES, measured at import after the file's declared unit scale; `unitScale` — metres per source unit as the FILE declared it (FBX UnitScaleFactor/100, 1 for formats that declare none); `fitKind` ('character' when the file carries a skeleton, else 'object'); `fitScale` — what every instantiation multiplies the root node's scale by (1 = the model measured plausible and is placed exactly as authored); `fitReason` — one sentence, present only when a fit was inferred; and `fitSource` ('auto' = the policy, 'manual' = assets.setFit).",
          Needs::Document },
        { "setFit", "assets.setFit(guid, {scale} | {reset: true} | {remeasure: true}) -> {extent, fitScale, fitReason, fitSource, fitKind}",
          "Overrides, restores or recomputes a MODEL asset's fit-to-size factor — the Assets page's "
          "\"Imported size\" row, as a verb. Exactly one option: `scale` is a positive multiplier "
          "recorded as a MANUAL fit (fitSource 'manual'); `reset: true` throws the manual override "
          "away and recomputes the automatic fit from the recorded extent; `remeasure: true` "
          "re-measures the model from its stored source file and recomputes the fit from that "
          "(the page's Re-measure — for a row imported before this feature existed, or one whose "
          "source was replaced). Returns the resulting block, so a caller never has to re-read it. "
          "The fit is a property of the ASSET, applied at the root node wherever the asset is "
          "instantiated, so this changes every FUTURE placement and no node already in a scene. "
          "Refuses a non-model asset, an unknown guid, a non-positive scale, and any combination "
          "other than exactly one of the three. NOT undoable — asset mutations never are "
          "(SCRIPTING_SPEC §1.6.5).",
          Needs::Document },
        { "rename", "assets.rename(guid, name) -> bool",
          "Renames a library asset — the Assets page's name field + Update button, as a verb. "
          "The row's TAGS are carried through untouched (both live in one write). Renaming "
          "changes the display name only: the guid every project pin, dependency row and scene "
          "reference uses is unchanged, so nothing breaks and nothing needs re-pinning. "
          "Refuses an empty name and an unknown guid. NOT undoable — asset mutations never are "
          "(SCRIPTING_SPEC \u00a71.6.5).",
          Needs::Document },
        { "setTags", "assets.setTags(guid, [tags]) -> [tags]",
          "Replaces a library asset's tags and returns the list as stored — the Assets page's "
          "tag field, as a verb. Tags are free text used for FINDING things "
          "(assets.list({tag: \"kitchen\"})); blanks are dropped and case-insensitive duplicates "
          "collapse, so the returned list is the truth rather than an echo. Pass an empty array "
          "to clear them. The asset's NAME is carried through untouched. NOT undoable.",
          Needs::Document },
        { "tags", "assets.tags(guid) -> [tags]",
          "The asset's tags, [] when it has none (also in assets.metadata's read).",
          Needs::Document },
        { "import", "assets.import(path) -> guid",
          "Imports a mesh file (obj, fbx, dae, glb, gltf, ply, stl — Constants::MODEL_EXTS) into the global asset store. NOT undoable. "
          "THE TYPE FOLLOWS THE FILE, not the extension: a model file that carries animation and NO geometry — a Mixamo download 'without skin', a .bvh capture — is stored as an ANIMATION asset (its own library type; every mesh path refuses a zero-mesh file), while a file with meshes stays an object even when it also carries clips. "
          "Read the type back with assets.metadata(guid).kind or assets.list({type: 'animation'}).",
          Needs::Document },
        { "importFile", "assets.importFile(path, drawerId?, {typeHint}) -> guid",
          "Imports any library-supported file (models, animation clips, images, audio, video) into the asset store, optionally filed in a drawer. Images/audio/video are headless-safe (video decodes through Qt Multimedia's ffmpeg backend, no display needed). NOT undoable. "
          "`typeHint` overrides the pipeline's SNIFF with an asset type name (the assets.list vocabulary: object, animation, texture, music, video, file, ...) — for the file whose extension lies, or the one the sniffer will not claim. It is a HINT to the importer selection, not a relabel of the result: a hint the pipeline cannot honour fails rather than filing bytes under the wrong kind. Unknown names are refused with the list.",
          Needs::Document },
        { "drawers", "assets.drawers() -> [{id, name, parent}]",
          "The asset drawers (nested collections). parent -1 = top level; Uncategorized is drawer 0.",
          Needs::Document },
        { "createDrawer", "assets.createDrawer(name, parentId?) -> id",
          "Creates a drawer, optionally nested under an existing one (default: top level). Returns the new drawer's id.",
          Needs::Document },
        { "renameDrawer", "assets.renameDrawer(id, name) -> bool",
          "Renames a drawer. The virtual root (-1) is not renamable.",
          Needs::Document },
        { "deleteDrawer", "assets.deleteDrawer(id) -> bool",
          "Deletes a drawer and its sub-drawers; the subtree's assets move to Uncategorized. Drawer 0 and the root are refused.",
          Needs::Document },
        { "moveDrawer", "assets.moveDrawer(id, parentId) -> bool",
          "Reparents a drawer (parentId -1 = top level). Cycles are refused.",
          Needs::Document },
        { "moveToDrawer", "assets.moveToDrawer(guid, id) -> bool",
          "Files a store asset in a drawer (0 = Uncategorized).",
          Needs::Document },
        { "addToProject", "assets.addToProject(storeGuid) -> guid",
          "Adds a library asset to the open project and returns its guid — THE SAME GUID: this is "
          "reference-with-pin, not a copy. The project gets a `project_assets` row pinning the "
          "asset (and its dependency closure) at the content it has RIGHT NOW, so the project "
          "keeps rendering exactly those bytes even after the library asset is re-imported; a "
          "project-side edit is copy-on-write (new content, this project's pin moves, the library "
          "untouched) and `assets.updateFromLibrary` re-pins to the library's current version. "
          "No files are copied and no rows are cloned. NOT undoable.",
          Needs::Document },
        { "updateFromLibrary", "assets.updateFromLibrary(guid) -> {guid, version}",
          "Re-pins the open project's version of a library asset to the library's CURRENT content "
          "— the other half of reference-with-pin. This DISCARDS the project's own edits to that "
          "asset (they live in the pinned content, which the pin stops naming); the library, and "
          "every other project, is unaffected either way. Returns the new pinned content id. "
          "Refuses an asset the project has not added. NOT undoable — asset mutations never are "
          "(SCRIPTING_SPEC \u00a71.6.5).",
          Needs::Document },
        { "addToScene", "assets.addToScene(guid, {position}) -> nodeId",
          "Instantiates a project object asset into the scene (undoable, like a drag from the asset browser).",
          Needs::Document },
        { "importAndPlace", "assets.importAndPlace(path, {position, drawer}) -> {assetGuid, projectGuid, nodeId}",
          "THE way to get a model file on disk into the open scene, in one call: it runs the one "
          "import pipeline (assets.importFile), pins the result into the open project "
          "(assets.addToProject) and instantiates it (assets.addToScene) — the three-call flow "
          "scene.addMesh used to short-circuit into a node that came back empty on the next open. "
          "`drawer` files the library asset in a drawer (id from assets.drawers()); `position` "
          "places the node (without it, it spawns in front of the editor camera). "
          "PARTLY UNDOABLE, and the split matters: the import and the pin are PERMANENT (an undo "
          "removes the node, not the library asset), only the placement is on the undo stack. Any "
          "other option key is refused rather than ignored.",
          Needs::Document },
        { "builtins", "assets.builtins() -> [{guid, name, kind}]",
          "The reserved built-ins: primitives and materials with their reserved guids. Guids collide "
          "across kinds — always pair guid with kind. The Default/Flat/Glass family reports kind "
          "'material' since HLMS adoption retired CustomMaterial: those rows ARE material presets, "
          "and reported 'shader' only because the map they live in is still called BuiltinShaders. "
          "Nothing resolves BY kind — a scene, a script or a saved document referencing one of these "
          "guids works exactly as before, and assets.list still accepts type 'shader' for the "
          "graph-backed material assets that really are ModelTypes::Shader rows.",
          Needs::Document },
        { "remove", "assets.remove(guid, {keepShared: true, force: false}) -> bool",
          "Removes a store asset from the LIBRARY. A LIBRARY DELETE NEVER TAKES AN ASSET OUT OF A PROJECT (owner law, 2026-09-09): "
          "if any project pins this asset (assets.pins lists them) the row is UNLISTED instead of deleted — it disappears from the library listing (assets.list({scope: 'store'}), the Assets page grid) and NOTHING else changes: its content mapping, every project pin, its dependency edges, its sidecar and its stored bytes all stay, and every project that pinned it keeps opening, rendering, thumbnailing and exporting it exactly as before (everything resolves BY GUID; assets.metadata still answers, with listed: false). The call returns true and sets no error. "
          "With NO pins it is the full delete: catalog rows, sidecar, whatever the retired per-guid folder left behind, and (keepShared false) its dependency assets too — each of those judged by its OWN pins. "
          "{force: true} is the hard delete (\"delete everywhere\"): the pins are dropped and the rows go whether or not projects used it. "
          "The asset's CONTENT is not unlinked in any of these cases — objects can be shared or pinned, so reclaiming them is assets.gc's job (an unlisted asset's bytes stay REFERENCED there; a force-deleted one's become reclaimable). PERMANENT — no undo.",
          Needs::Document },
        { "removeFromProject", "assets.removeFromProject(guid) -> bool",
          "Takes an asset OUT OF THE OPEN PROJECT and leaves the library alone: the project's "
          "pin on the asset goes, and the pins on the dependency members only this asset uses "
          "(a texture two pinned models share keeps its pin). Removing an IMAGE also removes the "
          "companion PBR material that adding it minted, when that material depends on nothing but "
          "this image, no asset depends on it and the project's scene does not name it. "
          "The library row, its content and "
          "every other project's pin are untouched. A row that was already removed from the "
          "library (assets.remove on a pinned asset) and has just lost its LAST pin is deleted "
          "for good, because nothing can reach it any more. This is what the project panel's "
          "Delete does; assets.remove is the library's delete. False with app.lastError when "
          "no project is open or the guid is unknown.",
          Needs::Document },
        { "pins", "assets.pins(guid) -> [{project, name, live}]",
          "Which PROJECTS pin this asset — the reference-with-pin rows that make a library delete an unlist. "
          "`project` is the project's guid, `name` its display name. "
          "`live` is false for a DEAD pin: the project row is gone, so the row names nobody ('(deleted project)') — it is reported because it is still a real catalog reference holding content alive, but it does NOT make a delete an unlist (assets.metadata's pinCount counts live pins only) and assets.gc's deadPins class reaps it. "
          "No LIVE entries means the asset is used by no project, i.e. assets.remove would really delete it. "
          "Reads the catalog only — the project does not have to be open, and the asset does not have to be listed.",
          Needs::Document },
        { "refreshThumbnail", "assets.refreshThumbnail(guid) -> bool",
          "Rebuilds an asset's thumbnail synchronously and writes it to the database. Objects, materials and shader graphs render on the engine (engine required; a shader renders the material its graph evaluates to, on the preview sphere); images re-thumbnail from the source file, videos re-grab a first-second frame, and audio/file rows reset to their type icon (document-only).",
          Needs::Document },
        { "thumbnail", "assets.thumbnail(guid) -> {guid, empty, bytes, width, height, centre: {r, g, b}}",
          "The thumbnail stored for an asset, as facts rather than pixels: byte size of the PNG blob, its decoded dimensions and the colour of its centre pixel (0-255). empty is true when the row carries no image. Document-only — it reads the database, it does not render.",
          Needs::Document },
        { "exportRaw", "assets.exportRaw(guid, dir, {dependencies: true, hash: true}) -> {dir, manifest, files, assets, totalBytes, warnings}",
          "Exports a store asset's files (and, by default, its dependencies' files) as loose files with their original names into dir, plus a jah.manifest.json (manifest v2: guids, types, dependency edges, sizes, sha256 content ids — hashing skippable via {hash: false}). Identical bytes are written once; assets with no stored files still get manifest entries. The unified-export front half (ASSET_PIPELINE_SPEC §3.3); .jaf export joins it in the final half.",
          Needs::Document },
        { "dependencies", "assets.dependencies(guid) -> [guid]",
          "The asset plus all its dependencies, recursively.",
          Needs::Document },
        { "storeRoot", "assets.storeRoot() -> path",
          "The active asset-store root directory (the assets/storeRoot setting; the AppData default when unset).",
          Needs::Document },
        { "setStoreRoot", "assets.setStoreRoot(path, {move, force}) -> bool",
          "Repoints the asset store. Empty path returns to the default root. {move: true} copies the current store's contents to the new root first (verified; the old tree is retained). Without move, the target must already contain this library's store ({force: true} skips that check). Throws on failure; nothing changes on a failed call.",
          Needs::Document },
        { "storeStatus", "assets.storeStatus() -> {root, online, missing}",
          "Store reachability: the active root, whether it is reachable (offline mode keeps the catalog fully usable), and how many library rows have no folder under it.",
          Needs::Document },
        { "importSettings", "assets.importSettings(guid) -> {sourceOid, importer, importerVersion, assimp, settings}",
          "The determinism record the ONE import pipeline stamped on the asset: content id of the source, "
          "importer name/version, assimp version and the request settings.",
          Needs::Document },
        { "checkConsistency", "assets.checkConsistency(guid) -> {consistent, expected, produced, ...}",
          "Re-runs the import pipeline's convert stage on the stored source and diffs the produced object "
          "set against the catalog (Unity -consistencyCheck): non-deterministic importers surface here.",
          Needs::Document },
        { "verify", "assets.verify({dbPath, root}) -> report",
          "Re-hashes every catalogued object against its oid: reports corrupt (bit-rot) and missing objects with counts and bytes. Defaults to the live library.",
          Needs::Document },
        { "rebuildCatalog", "assets.rebuildCatalog(dbPath, {root}) -> report",
          "Reconstructs catalog rows (assets + files + asset_files) from the store's sidecar/*.json into the given database — the disaster-recovery path. dbPath is REQUIRED (rebuilding into the live catalog is not implied); existing guids are left untouched; thumbnails are regenerable, not recovered. Sidecars whose recorded objects are ALL absent are skipped as tombstones (reported as `skipped`) — an old store's leftover sidecars must not resurrect deleted assets.",
          Needs::Document },
        { "gc", "assets.gc({dryRun: true, root, force}) -> report",
          "Store garbage collection. Finds — and, with {dryRun: false}, removes — six classes of leaked artifact: "
          "unreferencedObjects (a files row no asset_files row and no project pin names, with its object), "
          "strayObjects (files under objects/ the catalog never recorded, plus staging temps abandoned for over an hour), "
          "straySidecars (sidecar/<guid>.json naming no asset), "
          "legacyFolders (a <root>/<guid>/ folder from the retired per-guid view naming no asset), "
          "redundantLegacyFiles (entries in a LIVE asset's legacy folder whose bytes are byte-for-byte present in objects/) and "
          "deadPins (project_assets rows naming a project that no longer exists — catalog rows rather than files, so they free no bytes here; an object a dead pin was the last reference to becomes an unreferencedObjects item on the NEXT sweep). "
          "DRY RUN BY DEFAULT: the report lists exactly what a real run would delete, per class, with paths and byte totals. "
          "Live content is never collected — reachability is read from the asset_files rows and the project_assets pins, not from the refcount cache (a copy-on-write edit's object is referenced ONLY by its project's pin), and the sweep refuses a store this catalog does not recognize unless {force: true}. "
          "{root} sweeps an explicit store root instead of the active one.",
          Needs::Document },
        { "bakeAll", "assets.bakeAll({dryRun: true}) -> {needed, baked, failed, errors}",
          "Builds the MESH BAKE (the parsed, ready-to-load form of a model — MESH_BAKE_SPEC phase 1) for every "
          "stored MODEL FILE that has none or whose bake this build cannot read (counted by content, not by "
          "asset row: one object backs however many rows name it and needs exactly one bake). A bake is DERIVED data: the source "
          "file stays the truth and is never touched, and a bake is rebuilt whenever the code that produces it "
          "changes. Opening a world then LOADS its geometry instead of re-parsing every model with assimp. "
          "DRY RUN BY DEFAULT: reports how many assets would be baked without writing anything. "
          "Assets are baked lazily on first open too, so this is the bulk/explicit form — the button beside "
          "Preferences \u2192 Assets\u2019 storage cleanup.",
          Needs::Document },
    };
}

QVariantList AssetsApi::list(const QVariantMap &options)
{
    QVariantList out;
    if (!host.db) { fail("assets: not available in this session"); return out; }

    const QString scope = options.value("scope", "store").toString().toLower();
    int typeFilter = -1;
    if (options.contains("type")) {
        typeFilter = typeFromName(options.value("type").toString());
        if (typeFilter < 0) { fail(QStringLiteral("assets.list: unknown type '%1'").arg(options.value("type").toString())); return out; }
    }

    // The browse filters (AI_SURFACE_PROGRAM_SPEC lane C #6). They live on the
    // VERB, not in the MCP tool: browse_assets is only the byte-carrying view
    // of this listing, so anything a script cannot ask for is a capability the
    // tool would own alone.
    const QString query = options.value("query").toString().trimmed();
    // The TAG filter (F3): tags have been a library column and an Assets-page
    // text field since forever, with nothing on the verb surface able to read
    // or write one. Resolved per candidate row — it is the last filter applied,
    // so it only costs a query for rows that survived type/drawer/name.
    const QString tag = options.value("tag").toString().trimmed();
    const auto tagMatches = [this, &tag](const QString &guid) {
        if (tag.isEmpty()) return true;
        const QStringList tags = assettags::tagsOf(host.db, guid);
        for (const QString &candidate : tags)
            if (candidate.compare(tag, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    // The RIGGED filter (AVATAR_ASSET_SPEC §5.1): "which library models could
    // become an avatar". It reads the metadata block's `hasSkeleton`, through
    // `ensure` so a row imported before the rig fields existed backfills once
    // instead of being invisible forever. Applied AFTER the cheap filters for
    // that reason — it can cost one assimp parse per row it actually reaches.
    const bool riggedOnly = options.value("rigged", false).toBool();
    const auto riggedMatches = [this, riggedOnly](const AssetRecord &record) {
        if (!riggedOnly) return true;
        if (record.type != static_cast<int>(ModelTypes::Object)
            && record.type != static_cast<int>(ModelTypes::Mesh))
            return false;
        return AssetMetadata::ensure(host.db, record.guid)
            .value(QStringLiteral("hasSkeleton")).toBool();
    };
    const bool hasDrawer = options.contains("drawer");
    const int drawerFilter = options.value("drawer", -1).toInt();
    const int limit = options.value("limit", 0).toInt();
    const auto nameMatches = [&query](const QString &name) {
        return query.isEmpty() || name.contains(query, Qt::CaseInsensitive);
    };
    // Applied where the rows are built, so `limit` bounds the WORK too on the
    // store path (a 5000-row library browsed with limit 12 stops at 12).
    const auto full = [&out, limit]() { return limit > 0 && out.size() >= limit; };

    QVector<AssetRecord> records;
    if (scope == "session") {
        if (!tag.isEmpty()) {
            fail("assets.list: scope 'session' carries no tags — the live registrations are "
                 "not catalog rows. List scope 'store'/'project' to filter by tag");
            return out;
        }
        if (hasDrawer) {
            fail("assets.list: scope 'session' carries no drawer — drop the drawer filter "
                 "or list scope 'store'/'project'");
            return out;
        }
        if (riggedOnly) {
            fail("assets.list: scope 'session' carries no metadata — a rigged filter has "
                 "nothing to read there. List scope 'store'/'project'");
            return out;
        }
        // The live AssetManager registrations — what the viewport's drag-drop
        // lookups and the panels actually see. Makes session hydration
        // observable to scripts and tests (IMAGE_PLANE_SPEC §6 gate).
        if (!requireProject()) return out;
        for (Asset *asset : AssetManager::getAssets()) {
            if (!asset) continue;
            if (typeFilter >= 0 && static_cast<int>(asset->type) != typeFilter) continue;
            if (!nameMatches(asset->fileName)) continue;
            if (full()) break;
            out.append(QVariantMap{ { "guid", asset->assetGuid },
                                    { "name", asset->fileName },
                                    { "type", typeName(static_cast<int>(asset->type)) } });
        }
        return out;
    }
    if (scope == "store") {
        records = host.db->fetchAssetsForAssetView();
    } else if (scope == "project") {
        if (!requireProject()) return out;
        if (typeFilter >= 0) {
            // Folder-independent: a type-filtered project listing must see
            // assets registered in subfolders too (a preset apply files its
            // material asset under Presets/, which a root-children sweep
            // never returned).
            records = host.db->fetchFilteredAssets(host.project->getProjectGuid(), typeFilter);
            for (auto &record : records) record.type = typeFilter;   // this query selects name+guid only
            // ...and the PINNED members of that type: a pinned library asset
            // is a project member exactly like a per-project row (the
            // unfiltered branch below already unions them; leaving the
            // filtered one out hid pinned materials — e.g. the companion
            // image material — from every type-filtered listing).
            for (const auto &record :
                 host.db->fetchProjectPinnedAssets(host.project->getProjectGuid())) {
                if (record.type != typeFilter) continue;
                if (std::any_of(records.begin(), records.end(),
                                [&](const AssetRecord &r) { return r.guid == record.guid; }))
                    continue;
                records.append(record);
            }
            for (const auto &record : records) {
                if (hasDrawer && record.collection != drawerFilter) continue;
                if (!nameMatches(record.name)) continue;
                if (!tagMatches(record.guid)) continue;
                if (!riggedMatches(record)) continue;
                if (full()) break;
                out.append(QVariantMap{ { "guid", record.guid },
                                        { "name", record.name },
                                        { "type", typeName(typeFilter) },
                                        { "drawer", record.collection } });
            }
            return out;
        }
        records = host.db->fetchChildAssets(host.project->getProjectGuid(), host.project->getProjectGuid(), -1, true);
        // Reference-with-pin (phase 4): pinned LIBRARY assets are project
        // members too — a project "use" is a project_assets row, not a
        // cloned Editor row. fetchProjectPinnedAssets is the shared source
        // (the editor's project panel reads the same rows).
        for (const auto &record :
             host.db->fetchProjectPinnedAssets(host.project->getProjectGuid())) {
            if (std::any_of(records.begin(), records.end(),
                            [&](const AssetRecord &r) { return r.guid == record.guid; }))
                continue;
            records.append(record);
        }
    } else {
        fail("assets.list: scope must be 'store', 'project' or 'session'");
        return out;
    }

    for (const auto &record : records) {
        if (typeFilter >= 0 && record.type != typeFilter) continue;
        if (hasDrawer && record.collection != drawerFilter) continue;
        if (!nameMatches(record.name)) continue;
        if (!tagMatches(record.guid)) continue;
        if (!riggedMatches(record)) continue;
        if (full()) break;
        out.append(QVariantMap{ { "guid", record.guid },
                                { "name", record.name },
                                { "type", typeName(record.type) },
                                { "drawer", record.collection } });
    }
    return out;
}

QVariantMap AssetsApi::metadata(const QString &guid)
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }

    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        fail(QStringLiteral("assets.metadata: no asset with guid '%1'").arg(guid));
        return out;
    }

    // The lazy backfill: computes + persists the block when absent.
    out = AssetMetadata::ensure(host.db, guid).toVariantMap();
    out["guid"] = record.guid;
    out["name"] = record.name;
    out["type"] = typeName(record.type);
    // The tag list belongs in the row's own read (F3): it was a library column
    // nothing on the verb surface could see, so an asset browser built on
    // these verbs could not show what the Assets page shows.
    out["tags"] = QVariant(assettags::parse(record.tags));
    // LIBRARY VISIBILITY + USE (library delete keeps project pins): `listed`
    // false means a library delete unlisted this row because projects still
    // pin it — it resolves by guid exactly as before, it is simply not a
    // library tile. `pinCount` is how many projects pin it (assets.pins names
    // them), and is what decides which of the two a delete would do.
    out["listed"] = record.listed;
    out["pinCount"] = host.db->countAssetPins(guid);
    if (record.dateCreated.isValid())
        out["imported"] = record.dateCreated.toString(Qt::ISODate);
    return out;
}

QVariantList AssetsApi::pins(const QString &guid)
{
    QVariantList out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    if (host.db->fetchAsset(guid).guid.isEmpty()) {
        fail(QStringLiteral("assets.pins: no asset with guid '%1'").arg(guid));
        return out;
    }
    for (const AssetPinRecord &pin : assetdelete::pins(host.db, guid))
        out.append(QVariantMap{ { "project", pin.projectGuid },
                                { "name", pin.projectName },
                                { "live", pin.live } });
    return out;
}

// FIT TO SIZE (services/fitsize.h): the override half. The measurement and the
// automatic policy run at import; this is how a user disagrees with them, and
// how a library imported before the feature existed gets measured.
QVariantMap AssetsApi::setFit(const QString &guid, const QVariantMap &options)
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }

    static const QStringList known = { "scale", "reset", "remeasure" };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            fail(QStringLiteral("assets.setFit: unknown option '%1' (known: %2)")
                     .arg(it.key(), known.join(", ")));
            return out;
        }
    }
    const bool wantScale = options.contains(QStringLiteral("scale"));
    const bool wantReset = normalizeJs(options.value(QStringLiteral("reset"))).toBool();
    const bool wantRemeasure = normalizeJs(options.value(QStringLiteral("remeasure"))).toBool();
    if (int(wantScale) + int(wantReset) + int(wantRemeasure) != 1) {
        fail("assets.setFit: pass exactly one of {scale}, {reset: true} or {remeasure: true}");
        return out;
    }

    double scale = 0.0;
    if (wantScale) {
        bool ok = false;
        scale = normalizeJs(options.value(QStringLiteral("scale"))).toDouble(&ok);
        if (!ok || !(scale > 0.0) || !std::isfinite(scale)) {
            fail("assets.setFit: 'scale' must be a positive number");
            return out;
        }
    }

    const auto change = wantScale     ? AssetMetadata::FitChange::Manual
                      : wantRemeasure ? AssetMetadata::FitChange::Remeasure
                                      : AssetMetadata::FitChange::Reset;
    QString error;
    // ONE write (AssetMetadata::writeFit) — the Assets page's "Imported size"
    // row calls the same one, so a clicked change and a scripted one cannot
    // produce different blocks.
    const QJsonObject meta = AssetMetadata::writeFit(host.db, guid, change, scale, &error);
    if (meta.isEmpty()) {
        fail(QStringLiteral("assets.setFit: %1").arg(error));
        return out;
    }

    out = meta.toVariantMap();
    out["guid"] = guid;
    return out;
}

bool AssetsApi::rename(const QString &guid, const QString &name)
{
    if (!host.db) return fail("assets: not available in this session");
    const QString wanted = name.trimmed();
    if (wanted.isEmpty())
        return fail("assets.rename: a name is required (renaming to '' would leave a row nothing "
                    "can be found by)");
    if (host.db->fetchAsset(guid).guid.isEmpty())
        return fail(QStringLiteral("assets.rename: no asset with guid '%1'").arg(guid));
    if (!assettags::rename(host.db, guid, wanted))
        return fail(QStringLiteral("assets.rename: the database refused the rename of '%1'")
                        .arg(guid));
    return true;
}

QVariantList AssetsApi::setTags(const QString &guid, const QVariant &tags)
{
    QVariantList out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    if (host.db->fetchAsset(guid).guid.isEmpty()) {
        fail(QStringLiteral("assets.setTags: no asset with guid '%1'").arg(guid));
        return out;
    }
    const QVariant value = normalizeJs(tags);
    QStringList wanted;
    if (value.typeId() == QMetaType::QVariantList) {
        for (const QVariant &entry : value.toList()) wanted << entry.toString();
    } else if (value.typeId() == QMetaType::QString) {
        // One tag as a bare string is the obvious mistake; taking it is kinder
        // than refusing, and unambiguous (a tag can never contain a list).
        wanted << value.toString();
    } else if (value.isValid() && !value.isNull()) {
        fail(QStringLiteral("assets.setTags: expects an array of tag strings, got '%1'")
                 .arg(value.toString()));
        return out;
    }
    if (!assettags::setTags(host.db, guid, wanted)) {
        fail(QStringLiteral("assets.setTags: the database refused the write for '%1'").arg(guid));
        return out;
    }
    for (const QString &tag : assettags::tagsOf(host.db, guid)) out << tag;
    return out;
}

QVariantList AssetsApi::tags(const QString &guid)
{
    QVariantList out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        fail(QStringLiteral("assets.tags: no asset with guid '%1'").arg(guid));
        return out;
    }
    for (const QString &tag : assettags::parse(record.tags)) out << tag;
    return out;
}

QString AssetsApi::import(const QString &path)
{
    if (!host.services || !host.services->assets) { fail("assets: not available in this session"); return QString(); }
    const auto result = host.services->assets->importMesh(path);
    if (!result.ok()) {
        fail(QStringLiteral("assets.import: %1").arg(result.error));
        return QString();
    }
    // Best effort thumbnail when the engine is up; headless-doc runs skip it.
    // OBJECTS ONLY: the engine render exists because a mesh import's tile is
    // otherwise blank. Every other type this entry point can now produce (an
    // ANIMATION asset — a model file with no geometry) was given its final
    // thumbnail by its importer, and asking for a rebuild here threw the whole
    // verb AFTER a successful import, which is a passing import reported as a
    // failure (found driving the real UI, where the engine IS up).
    if (host.isEngineReady()
        && host.db->fetchAsset(result.objectGuid).type == static_cast<int>(ModelTypes::Object))
        refreshThumbnail(result.objectGuid);
    return result.objectGuid;
}

QString AssetsApi::importFile(const QString &path, int drawerId, const QVariantMap &options)
{
    if (!host.services || !host.services->assets) { fail("assets: not available in this session"); return QString(); }
    // The pipeline has always had a typeHint on its ImportRequest (the .jaf
    // and drag-drop paths set it); nothing on the verb surface could reach it
    // (F18), so a file the sniffer reads wrong had no scripted way in.
    static const QStringList knownOptions = { QStringLiteral("typeHint") };
    const QString refusal = refuseUnknownKeys(QStringLiteral("assets.importFile"), options,
                                              knownOptions);
    if (!refusal.isEmpty()) { fail(refusal); return QString(); }
    int typeHint = -1;
    if (options.contains(QStringLiteral("typeHint"))) {
        const QString hint = options.value(QStringLiteral("typeHint")).toString();
        typeHint = typeFromName(hint);
        if (typeHint < 0) {
            fail(QStringLiteral("assets.importFile: unknown typeHint '%1' (object, mesh, texture, "
                                "material, sky, music, video, shader, particles, lightprofile, file)")
                     .arg(hint));
            return QString();
        }
    }
    const auto result = host.services->assets->importFile(path, drawerId, typeHint);
    if (!result.ok()) {
        fail(QStringLiteral("assets.importFile: %1").arg(result.error));
        return QString();
    }
    if (!result.error.isEmpty()) {   // imported, but the drawer filing failed
        fail(QStringLiteral("assets.importFile: %1").arg(result.error));
        return QString();
    }
    // Meshes get their preview render when the engine is up (like assets.import).
    if (host.isEngineReady() && host.db
        && host.db->fetchAsset(result.objectGuid).type == static_cast<int>(ModelTypes::Object))
        refreshThumbnail(result.objectGuid);
    return result.objectGuid;
}

QVariantList AssetsApi::drawers()
{
    QVariantList out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    for (const auto &coll : host.db->fetchCollections())
        out.append(QVariantMap{ { "id", coll.id },
                                { "name", coll.name },
                                { "parent", coll.parent } });
    return out;
}

int AssetsApi::createDrawer(const QString &name, int parentId)
{
    if (!host.db) { fail("assets: not available in this session"); return -1; }
    if (name.trimmed().isEmpty()) { fail("assets.createDrawer: the name is empty"); return -1; }
    const int id = host.db->createCollection(name.trimmed(), parentId);
    if (id < 0) fail(QStringLiteral("assets.createDrawer: no drawer with id %1 to nest under").arg(parentId));
    return id;
}

bool AssetsApi::renameDrawer(int id, const QString &name)
{
    if (!host.db) return fail("assets: not available in this session");
    if (name.trimmed().isEmpty()) return fail("assets.renameDrawer: the name is empty");
    if (id < 0 || host.db->fetchCollectionSubtree(id).isEmpty())
        return fail(QStringLiteral("assets.renameDrawer: no drawer with id %1").arg(id));
    return host.db->renameCollection(id, name.trimmed());
}

bool AssetsApi::deleteDrawer(int id)
{
    if (!host.db) return fail("assets: not available in this session");
    if (id <= 0)
        return fail("assets.deleteDrawer: the root and Uncategorized are not deletable");
    if (host.db->fetchCollectionSubtree(id).isEmpty())
        return fail(QStringLiteral("assets.deleteDrawer: no drawer with id %1").arg(id));
    return host.db->deleteCollection(id);
}

bool AssetsApi::moveDrawer(int id, int parentId)
{
    if (!host.db) return fail("assets: not available in this session");
    if (!host.db->setCollectionParent(id, parentId))
        return fail(QStringLiteral("assets.moveDrawer: cannot move drawer %1 under %2 "
                                   "(unknown drawer, or the move would create a cycle)")
                        .arg(id).arg(parentId));
    return true;
}

bool AssetsApi::moveToDrawer(const QString &guid, int id)
{
    if (!host.db) return fail("assets: not available in this session");
    if (host.db->fetchAsset(guid).guid.isEmpty())
        return fail(QStringLiteral("assets.moveToDrawer: no asset with guid '%1'").arg(guid));
    if (id != 0 && host.db->fetchCollectionSubtree(id).isEmpty())
        return fail(QStringLiteral("assets.moveToDrawer: no drawer with id %1").arg(id));
    return host.db->switchAssetCollection(id, guid);
}

QString AssetsApi::addToProject(const QString &guid)
{
    if (!host.db) { fail("assets: not available in this session"); return QString(); }
    if (!requireProject()) return QString();

    // Reference-with-pin (phase 4): ONE implementation for verb and widget.
    // The project pins the asset (and its dependency closure) at its current
    // content - no file copies, no row clones. The returned guid IS the
    // library asset's guid.
    const auto result = ProjectAssets::addToProject(guid, host.db, host.project, ProjectAssets::AddKind::Direct);
    if (!result.ok()) {
        fail(QStringLiteral("assets.addToProject: %1").arg(result.error));
        return QString();
    }
    return result.guid;
}

QVariantMap AssetsApi::updateFromLibrary(const QString &guid)
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    if (!requireProject()) return out;

    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        fail(QStringLiteral("assets.updateFromLibrary: no asset with guid '%1'").arg(guid));
        return out;
    }
    QSqlDatabase conn = QSqlDatabase::database();
    if (AssetCas::pinnedOid(conn, host.project->getProjectGuid(), guid).isEmpty()) {
        fail(QStringLiteral("assets.updateFromLibrary: '%1' is not in this project — there is no "
                            "pin to update (assets.addToProject adds it)").arg(record.name));
        return out;
    }
    if (!ProjectAssets::updatePinToLatest(guid, host.db, host.project)) {
        fail(QStringLiteral("assets.updateFromLibrary: the pin of '%1' could not be moved")
                 .arg(record.name));
        return out;
    }
    // The re-pin is the EVENT every linked instance follows (AVATAR_ASSET §4
    // D4): announced through the assets service so the scene refreshes now
    // rather than at the next open.
    if (host.services && host.services->assets)
        host.services->assets->announcePinChanged(guid);

    out["guid"] = guid;
    out["version"] = AssetCas::pinnedOid(conn, host.project->getProjectGuid(), guid);
    return out;
}

QString AssetsApi::addToScene(const QString &guid, const QVariantMap &options)
{
    if (!host.db || !host.mainWindow) { fail("assets: not available in this session"); return QString(); }
    if (!requireProject()) return QString();

    // Type-qualified lookup (§1.6.4: reserved guids collide across kinds).
    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty() || record.type != static_cast<int>(ModelTypes::Object)) {
        fail(QStringLiteral("assets.addToScene: '%1' is not an object asset").arg(guid));
        return QString();
    }

    const bool hasPosition = options.contains("position");
    host.services->selection->select(iris::SceneNodePtr());
    host.services->sceneEdit->addMaterialMesh(QString(), hasPosition,
                                              vecFromJs(options.value("position")), guid, record.name);
    auto node = host.services->selection->selected();
    if (!node) {
        fail("assets.addToScene: the asset could not be instantiated");
        return QString();
    }
    return node->getGUID();
}

// AI_SURFACE_PROGRAM_SPEC lane D #7 — the other half of the F1 story.
//
// scene.addMesh(diskPath) existed because it LOOKED like the obvious verb; it
// wrote a disk path where the reader expects a guid and now refuses, naming
// this verb. So this composes the three real calls and nothing more: no fourth
// import route (ASSET_PIPELINE_SPEC: one pipeline), no new placement path.
//
// It is deliberately not "atomic". The import and the pin are permanent
// (assets.importFile / addToProject are documented NOT undoable); only
// addToScene pushes a command. Rolling the import back on a placement failure
// would mean deleting a library asset the user may already see in the browser
// — so instead the failure message says exactly which guids DID land, and the
// doc string says the split out loud rather than implying one undo covers it.
QVariantMap AssetsApi::importAndPlace(const QString &path, const QVariantMap &options)
{
    QVariantMap out;
    if (!host.services || !host.services->assets) {
        fail("assets: not available in this session");
        return out;
    }
    if (!requireProject()) return out;   // the pin needs an open project

    // Unknown keys are refused, never swallowed (the F7/F8/F9 class).
    static const QStringList known = { "position", "drawer" };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (!known.contains(it.key())) {
            fail(QStringLiteral("assets.importAndPlace: unknown option '%1' (known: %2)")
                     .arg(it.key(), known.join(", ")));
            return out;
        }
    }

    if (!QFileInfo::exists(path)) {
        fail(QStringLiteral("assets.importAndPlace: no such file: %1").arg(path));
        return out;
    }

    int drawerId = -1;
    if (options.contains("drawer")) {
        bool numeric = false;
        drawerId = options.value("drawer").toInt(&numeric);
        if (!numeric) {
            fail(QStringLiteral("assets.importAndPlace: 'drawer' must be a drawer id "
                                "(assets.drawers() lists them; 0 = Uncategorized)"));
            return out;
        }
    }

    // 1. the ONE import pipeline. Reuses the verb so a drawer-filing failure,
    //    a rejected format and the thumbnail rule are all decided in one place.
    const QString assetGuid = importFile(path, drawerId);
    if (assetGuid.isEmpty()) return out;   // importFile already threw
    out["assetGuid"] = assetGuid;

    const auto record = host.db ? host.db->fetchAsset(assetGuid) : AssetRecord();
    if (record.type != static_cast<int>(ModelTypes::Object)) {
        fail(QStringLiteral("assets.importAndPlace: '%1' imported as a %2 asset (%3), which cannot "
                            "be placed in the scene — only models can. The library asset IS there; "
                            "use assets.addToProject to pin it.")
                 .arg(QFileInfo(path).fileName(), typeName(record.type), assetGuid));
        return out;
    }

    // 2. pin it into the open project (reference-with-pin: same guid back).
    const QString projectGuid = addToProject(assetGuid);
    if (projectGuid.isEmpty()) return out;
    out["projectGuid"] = projectGuid;

    // 3. place it — the only undoable step.
    QVariantMap placeOptions;
    if (options.contains("position")) placeOptions["position"] = options.value("position");
    const QString nodeId = addToScene(projectGuid, placeOptions);
    if (nodeId.isEmpty()) return out;
    out["nodeId"] = nodeId;
    return out;
}

QVariantList AssetsApi::builtins()
{
    QVariantList out;
    const auto append = [&out](const QMap<QString, QString> &map, const char *kind) {
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            out.append(QVariantMap{ { "guid", it.key() }, { "name", it.value() }, { "kind", kind } });
    };
    append(Constants::Reserved::DefaultPrimitives, "primitive");
    append(Constants::Reserved::DefaultMaterials, "material");
    // "material", not "shader" (owner decision, 2026-09-07). The reserved
    // Default/Flat/Glass/Matcap family stopped being shaders when
    // CustomMaterial was retired (HLMS_ADOPTION P4b): every one of them
    // hydrates as a PbrMaterial preset (io/builtinmaterials.cpp) and the
    // "Material" picker lists them beside the graph-backed materials. The map
    // keeps its historical name; the REPORTED kind now tells the truth.
    // Purely a label: nothing looks these guids up by kind.
    append(Constants::Reserved::BuiltinShaders, "material");
    return out;
}

bool AssetsApi::remove(const QString &guid, const QVariantMap &options)
{
    if (!host.db) return fail("assets: not available in this session");

    static const QStringList known = { "keepShared", "force" };
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        if (!known.contains(it.key()))
            return fail(QStringLiteral("assets.remove: unknown option '%1' (known: %2)")
                            .arg(it.key(), known.join(", ")));
    }

    // THE one implementation, shared with the Assets page's Delete button
    // (services/assetdelete.h): a library delete unlists a pinned asset and
    // deletes an unpinned one, and {force: true} deletes either way.
    const bool keepShared = normalizeJs(options.value("keepShared", true)).toBool();
    const bool force = normalizeJs(options.value("force", false)).toBool();
    const auto outcome = assetdelete::remove(host.db, guid, keepShared, force);
    if (!outcome.ok) return fail(QStringLiteral("assets.remove: %1").arg(outcome.error));
    return true;
}

bool AssetsApi::removeFromProject(const QString &guid)
{
    if (!host.db) return fail("assets: not available in this session");
    if (!host.project || host.project->getProjectGuid().isEmpty())
        return fail("assets.removeFromProject: no project is open");
    const auto outcome = assetdelete::removeFromProject(host.db, guid, host.project->getProjectGuid());
    if (!outcome.ok) return fail(QStringLiteral("assets.removeFromProject: %1").arg(outcome.error));
    return true;
}

bool AssetsApi::refreshThumbnail(const QString &guid)
{
    if (!host.db) return fail("assets: not available in this session");

    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty())
        return fail(QStringLiteral("assets.refreshThumbnail: no asset with guid '%1'").arg(guid));

    // Document-only types first — no engine needed (headless-safe), mirroring
    // the tile's "Rebuild Thumbnail" action.
    if (record.type == static_cast<int>(ModelTypes::Texture)) {
        auto thumb = ThumbnailManager::createThumbnail(storeFileFor(guid), 256, 256);
        if (!thumb || thumb->thumb.isNull())
            return fail("assets.refreshThumbnail: could not read the image");
        return host.db->updateAssetThumbnail(
            guid, AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(thumb->thumb)));
    }
    if (record.type == static_cast<int>(ModelTypes::Music)) {
        return host.db->updateAssetThumbnail(
            guid, AssetHelper::makeBlobFromPixmap(
                      QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-music.png"))));
    }
    if (record.type == static_cast<int>(ModelTypes::Video)) {
        // First-second frame re-grab; VideoUtils falls back to the film icon
        // when decode fails, so this always writes something sensible.
        const QPixmap thumb = VideoUtils::thumbnailFor(storeFileFor(guid));
        return host.db->updateAssetThumbnail(guid, AssetHelper::makeBlobFromPixmap(thumb));
    }
    if (record.type == static_cast<int>(ModelTypes::Animation)) {
        // The POSE STRIP the import drew, redrawn — a clip file has no engine
        // render to make (there is nothing to put the clip ON), so this is the
        // same three projected poses, from the stored bytes. Document-only,
        // like the image and audio rows above it.
        QImage strip;
        animfile::read(storeFileFor(guid), &strip, 256, 256);
        if (strip.isNull())
            return fail("assets.refreshThumbnail: could not read the animation");
        return host.db->updateAssetThumbnail(
            guid, AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(strip)));
    }
    if (record.type == static_cast<int>(ModelTypes::File)) {
        return host.db->updateAssetThumbnail(
            guid, AssetHelper::makeBlobFromPixmap(
                      QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-72.png"))));
    }

    if (!requireEngine()) return false;
    auto engine = EngineHost::instance().engine();
    if (!engine) return fail("assets.refreshThumbnail: the engine is not running");

    QImage image;
    EngineThumbnailRenderer renderer(engine);
    if (record.type == static_cast<int>(ModelTypes::Object)) {
        // ALWAYS rebuild from the blob: the session-registered import node
        // carries texture properties rewritten to raw guids (no reader ever
        // resolved them), so rendering it gives the white, untextured
        // thumbnail this verb existed to replace. SceneReader + the database
        // handle resolves those guids to store files.
        SceneReader reader;
        reader.setDatabaseHandle(host.db);
        reader.setProject(host.project);
        // LIBRARY resolution: the store asset's own bytes, by guid.
        reader.setLibrarySource();
        auto blob = QJsonDocument::fromJson(host.db->fetchAssetData(guid)).object();
        iris::SceneNodePtr node = reader.readSceneNode(blob);
        if (!node) return fail("assets.refreshThumbnail: could not rebuild the object");
        image = renderer.renderNode(node, QSize(512, 512));
    } else if (record.type == static_cast<int>(ModelTypes::Material)) {
        auto matObject = QJsonDocument::fromJson(host.db->fetchAssetData(guid)).object();
        MaterialReader reader;
        reader.setProject(host.project);
        image = renderer.renderMaterial(reader.parseMaterialTyped(matObject, host.db), QSize(512, 512));
    } else if (record.type == static_cast<int>(ModelTypes::Shader)) {
        // A shader asset is a graph definition: it thumbnails as the material
        // the evaluator baked into it, on the same preview sphere a .material
        // uses (VISUAL_PARITY_SPEC item 5).
        MaterialReader reader;
        reader.setProject(host.project);
        auto material = reader.parseShaderAsPbr(guid, host.db);
        if (!material) {
            renderer.release();
            return fail("assets.refreshThumbnail: this shader carries no evaluated material "
                        "(re-save the graph, or run materials.regenerate; baked maps need an open project)");
        }
        image = renderer.renderMaterial(material, QSize(512, 512));
    } else {
        renderer.release();
        return fail("assets.refreshThumbnail: only object, material and shader assets are supported");
    }
    renderer.release();

    if (image.isNull()) return fail("assets.refreshThumbnail: the render produced no image");
    return host.db->updateAssetThumbnail(guid, AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(image)));
}

QVariantMap AssetsApi::thumbnail(const QString &guid)
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        fail(QStringLiteral("assets.thumbnail: no asset with guid '%1'").arg(guid));
        return out;
    }
    out["guid"] = guid;
    out["bytes"] = record.thumbnail.size();
    QImage image;
    image.loadFromData(record.thumbnail, "PNG");
    out["empty"] = image.isNull();
    out["width"] = image.isNull() ? 0 : image.width();
    out["height"] = image.isNull() ? 0 : image.height();
    if (!image.isNull()) {
        const QColor c = image.pixelColor(image.width() / 2, image.height() / 2);
        out["centre"] = QVariantMap{ { "r", c.red() }, { "g", c.green() }, { "b", c.blue() } };
    }
    return out;
}

QVariantList AssetsApi::dependencies(const QString &guid)
{
    QVariantList out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    for (const auto &dep : AssetHelper::fetchAssetAndAllDependencies(guid, host.db))
        out.append(dep);
    return out;
}

QVariantMap AssetsApi::exportRaw(const QString &guid, const QString &dir, const QVariantMap &options)
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }

    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        fail(QStringLiteral("assets.exportRaw: no asset with guid '%1'").arg(guid));
        return out;
    }
    if (dir.trimmed().isEmpty()) {
        fail("assets.exportRaw: a destination directory is required");
        return out;
    }
    const bool withDeps = options.value("dependencies", true).toBool();
    const bool hash = options.value("hash", true).toBool();

    QStringList guids{ record.guid };
    if (withDeps) {
        for (const QString &g : AssetHelper::fetchAssetAndAllDependencies(record.guid, host.db))
            if (!guids.contains(g)) guids.append(g);
    }

    QVector<RawExporter::AssetInfo> infos;
    for (const QString &g : guids) {
        const auto rec = host.db->fetchAsset(g);
        if (rec.guid.isEmpty()) continue;   // stale dependency edge — skip, not fatal
        RawExporter::AssetInfo info;
        info.guid = rec.guid;
        info.name = rec.name;
        info.typeId = rec.type;
        info.type = typeName(rec.type);
        info.dependencies = host.db->fetchAssetGUIDAndDependencies(rec.guid, false);
        infos.append(info);
    }

    // The resolver-backed source (final half): entries by oid through the
    // catalog. Library exports carry no project pin context.
    Q_UNUSED(hash);   // oids come from the catalog, not a re-hash
    CasContentSource source(AssetMetadata::storeRootPath());
    const auto r = RawExporter::exportAssets(infos, source, dir.trimmed());
    if (!r.ok) { fail(QStringLiteral("assets.exportRaw: %1").arg(r.error)); return out; }

    out["dir"] = r.dir;
    out["manifest"] = r.manifestPath;
    out["files"] = QVariant(r.exportedFiles);
    out["assets"] = r.assetCount;
    out["totalBytes"] = r.totalBytes;
    out["warnings"] = QVariant(r.warnings);
    return out;
}

QString AssetsApi::storeRoot()
{
    return AssetStorePaths::root();
}

bool AssetsApi::setStoreRoot(const QString &path, const QVariantMap &options)
{
    const bool move = options.value("move", false).toBool();
    const bool force = options.value("force", false).toBool();

    QString error;
    if (!AssetStoreService::setRoot(path, move, force,
                                    SettingsManager::getDefaultManager(),
                                    host.db, &error)) {
        return fail(QStringLiteral("assets.setStoreRoot: %1").arg(error));
    }
    return true;
}

QVariantMap AssetsApi::storeStatus()
{
    return AssetStoreService::status(host.db);
}

namespace
{
QString liveDbPath()
{
    return QDir(AppPaths::dataRoot())
        .filePath(Constants::JAH_DATABASE);
}
} // namespace

QVariantMap AssetsApi::verify(const QVariantMap &options)
{
    const QString dbPath = options.value("dbPath", liveDbPath()).toString();
    const QString root = options.value("root", AssetStorePaths::root()).toString();
    const auto report = AssetMigration::verify(dbPath, root);
    if (!report.error.isEmpty()) fail(QStringLiteral("assets.verify: %1").arg(report.error));
    return report.toMap();
}

QVariantMap AssetsApi::rebuildCatalog(const QString &dbPath, const QVariantMap &options)
{
    if (dbPath.isEmpty()) {
        fail("assets.rebuildCatalog: dbPath is required (rebuilding into the live catalog is never implied)");
        return QVariantMap();
    }
    const QString root = options.value("root", AssetStorePaths::root()).toString();
    const auto report = AssetMigration::rebuildCatalog(dbPath, root);
    if (!report.ok) fail(QStringLiteral("assets.rebuildCatalog: %1").arg(report.error));
    return report.toMap();
}

QVariantMap AssetsApi::gc(const QVariantMap &options)
{
    // DRY RUN IS THE DEFAULT, and it is the default HERE as well as in the
    // dialog: a caller that forgets the option gets a report, never a delete.
    const bool dryRun = options.value("dryRun", true).toBool();
    const bool force  = options.value("force", false).toBool();
    const QString root = options.value("root", AssetStorePaths::root()).toString();

    const auto report = AssetGc::sweep(QSqlDatabase::database(), root, dryRun, force);
    if (!report.ok && !report.error.isEmpty())
        fail(QStringLiteral("assets.gc: %1").arg(report.error));
    return report.toMap();
}

QVariantMap AssetsApi::importSettings(const QString &guid)
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    AssetImportService service(host.db, host.project);
    const QJsonObject record = service.importSettings(guid);
    if (record.isEmpty()) { fail(QStringLiteral("assets.importSettings: no import record for '%1'").arg(guid)); return out; }
    return record.toVariantMap();
}

QVariantMap AssetsApi::bakeAll(const QVariantMap &options)
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    const bool dryRun = options.value(QStringLiteral("dryRun"), true).toBool();

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QStringList needing = MeshBakeStore::modelSourcesNeedingBake(conn, root);

    QVariantList errors;
    int baked = 0, failed = 0;
    if (!dryRun) {
        for (const QString &path : needing) {
            QString error;
            if (MeshBakeStore::bakeSource(conn, root, path, &error)) {
                ++baked;
            } else {
                ++failed;
                errors.append(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), error));
            }
        }
        MeshBakeStore::clear();
    }

    out.insert(QStringLiteral("dryRun"), dryRun);
    out.insert(QStringLiteral("needed"), needing.size());
    out.insert(QStringLiteral("baked"), baked);
    out.insert(QStringLiteral("failed"), failed);
    out.insert(QStringLiteral("errors"), errors);
    return out;
}

QVariantMap AssetsApi::checkConsistency(const QString &guid)
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    AssetImportService service(host.db, host.project);
    const QJsonObject report = service.checkConsistency(guid);
    if (report.value("ok").toBool() == false)
        fail(QStringLiteral("assets.checkConsistency: %1").arg(report.value("error").toString()));
    return report.toVariantMap();
}
