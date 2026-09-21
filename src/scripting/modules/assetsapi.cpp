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

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
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
#include "services/assetshare.h"
#include "services/materialmembers.h"
#include "services/memberstamp.h"
#include "services/materialpresetassets.h"
#include "services/presetrestamp.h"
#include "services/animationfile.h"
#include "services/assetcas.h"
#include "services/assetgc.h"
#include "services/assetservice.h"
#include "services/assetmigration.h"
#include "services/assetstore.h"
#include "services/assettags.h"
#include "services/materialbundle.h"
#include "services/assettray.h"
#include "services/assetstorepaths.h"
#include "services/meshbakestore.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "jahshaka/engine/Types.h"
#include "data/constants.h"
#include "data/primitives.h"
#include "data/settingsmanager.h"
#include "services/assethelper.h"
#include "services/projectassets.h"
#include "services/projectfolders.h"
#include "commands/projectfoldercommand.h"
#include <QUndoStack>
#include <memory>
#include "services/import/assetimportservice.h"
#include "services/assetmetadata.h"
#include "services/thumbnailmanager.h"
#include "services/thumbnailrebuild.h"
#include "services/videoutils.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "bridge/assetthumbnail.h"
#include "bridge/engineassetscene.h"
#include "ui/pages/assetview.h"
#include "ui/pages/engineassetviewer.h"
#include "viewport/flystep.h"
#include "bridge/enginethumbnailrenderer.h"
#include "viewport/ieditorviewport.h"
#include "bridge/enginehost.h"
#include "io/assetmanager.h"
#include "io/materialreader.h"
#include "io/scenereader.h"
#include "shell/mainwindow.h"
#include "services/sceneeditservice.h"
#include "services/surfaceplacement.h"
#include "services/selectionservice.h"
#include "services/services.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "services/assetdelete.h"
#include "services/livetextures.h"

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
    // SESSION ONLY (MATERIAL_GAPS_SPEC A-1): no store or project listing can
    // ever contain one, so this name is only ever useful with scope 'session'.
    if (n == "livetexture" || n == "live") return static_cast<int>(ModelTypes::LiveTexture);
    return -1;
}

// The asset's stored bytes, by guid — the CAS resolver, never the retired
// <root>/<guid>/ view (deep audit 2026-09, area 6). Falls back to the legacy
// folder itself for stores that still have one (AssetCas::resolveSource).
QString storeFileFor(const QString &guid)
{
    return AssetCas::resolveSource(QSqlDatabase::database(), AssetStorePaths::root(), guid);
}

/// THE IMPORT-SETTINGS OPTIONS, from a verb's option map to the JSON record
/// (SPECS/IMPORT_DIALOG_SPEC.md §3/§7). Returns false with `errorOut` set when
/// a key is unknown or a value is refused — the SAME refusal
/// iris::ImportSettings::fromJson makes, so a script and the dialog cannot be
/// told different things.
bool importSettingsFromOptions(const QString &verb, const QVariantMap &options,
                               QJsonObject *recordOut, QString *errorOut)
{
    QJsonObject record;
    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        const QVariant value = scriptmod::normalizeJs(it.value());
        record.insert(it.key(), QJsonValue::fromVariant(value));
    }
    QString error;
    const iris::ImportSettings parsed = iris::ImportSettings::fromJson(record, &error);
    if (!error.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("%1: %2").arg(verb, error);
        return false;
    }
    // The COMPLETE record, not the caller's subset: one shape on the row, one
    // shape read back by assets.importSettings, one hash.
    if (recordOut) *recordOut = parsed.toJson();
    return true;
}

} // namespace

QVector<VerbInfo> AssetsApi::verbs() const
{
    return {
        { "list", "assets.list({scope: 'store'|'project'|'session', type, query, tag, drawer, rigged, tray, members, limit}) -> [{guid, name, type, drawer}]",
          "Store assets (default) or the open project's assets, optionally filtered by type name. A type-filtered project listing sweeps every folder (materials registered under Presets/ included); unfiltered it lists the root folder. drawer is the containing drawer's id (0 = Uncategorized). Scope 'session' lists the live session registrations (the AssetManager entries project open + add-to-project hydrate — what the editor's drag-drop paths look up); drawer is absent there. "
          "query is a case-insensitive substring match on the asset NAME; tag keeps only rows carrying that TAG (case-insensitive, exact — assets.setTags writes them, and scope 'session' has none, so a tag filter there is refused); drawer restricts the listing to one drawer id (0 = Uncategorized, refused for scope 'session', which carries no drawer); rigged: true keeps only MODEL rows whose metadata says the file carries a skeleton (the candidates avatar.createAsset accepts — refused for scope 'session', which has no metadata); limit caps how many rows come back (<= 0 means no cap). Filters apply in that order — type, then drawer, then query, then tag, then rigged — and limit last, so a limited listing is the first N of the filtered set, not a sample of it. rigged is the expensive one on a library that predates the rig metadata (it backfills the block once per row it reaches), which is why it is applied last. "
          "tray: true is THE EDITOR TRAY's listing, from the same function the tray panel calls (services/assettray.h) — EVERY ASSET THE PROJECT'S SCENE USES, ONCE (owner rules, 2026-09-11 and 2026-09-12): the root folder's rows plus the project's pinned members, where a row is dropped only when it is an import's MEMBER (its parent is another asset), a MESH row, a model or clip an AVATAR in this project is built from (the avatar is that character's tile), a scene node's OWN row (the built-in primitives, the Ground, image planes, decals, particle emitters — a node is not a library asset; what it uses is), or an image added directly whose companion material is the only thing in the project using it (the material is that image's tile). A dependency never hides anything: a texture on a material slot, the ground, a decal or a particle, a material applied to a node, all stay. One more row is folded: a picture that arrived THROUGH a material's picker, while only materials use it — the bundle is its tile (the owner's rule V-2). `members: true` turns off that one rule and lists them, which is the 'Show member textures' switch in the panels. With type, the same listing keeps one type. Nothing is deleted and every guid still resolves. Refused for scopes 'store' and 'session': the tray is a project's listing. SCOPE 'store' IS THE ASSETS PAGE's grid, from the one function the page reads (assettray::libraryList): import members (a Mesh, an import's own textures — rows whose parent is another asset) are never listed, a legacy Shader row is not, and the bundle rule above applies there too — a picture that arrived inside a material and that only materials use is folded into the bundle, `members: true` lists it (the page's own 'Show member textures' switch).",
          Needs::Document },
        { "metadata", "assets.metadata(guid) -> {guid, name, type, tags, imported, kind, format, fileSize, ...}",
          "Rich per-type metadata for a store asset. Models: vertices, triangles, meshes, materials, textures, plus the RIG block — hasSkeleton, bones, boneNames, nodeNames, rigId (a stable hash of the sorted bone names: two exports of one skeleton share it) and animations [{name, length in seconds, channels, boneChannels}]; images: width, height; audio (wav): duration (ms), sampleRate, channels, bitsPerSample; video: duration (ms), width, height, frameRate, videoCodec; every kind: format + fileSize. Computed at import since the metadata feature landed; for older rows the first call computes it from the store files and persists it (lazy backfill). "
          "`member` and `memberOf` are present only on a row that arrived INSIDE a material — a texture picked through a material\'s picker, or a shipped preset\'s map at the first-run seed: `member: true` is the stamp (MATERIAL_BUNDLE_SPEC V-2) and `memberOf` names the material it came in through. The stamp is what folds the picture\'s tile into the bundle\'s in the editor tray while ONLY materials use it; the moment a scene node, a decal or the user themselves uses it, it is a tile again. A texture the user imported carries neither key — and an import THE USER ASKS FOR takes the stamp off a row it lands on (assets.importFile), because the stamp is an origin and their intent outranks it. "
          "`companionOf` is present only on a MATERIAL that 'Create material from image' minted, and names the TEXTURE it was minted for — the stamp that makes an image and its own material relatable (and that keeps the image's tile folded into the material's in the editor tray). A material the user authored on the same image carries no stamp and no key. "
          "`tags` is the row's tag list (assets.setTags writes it, assets.list({tag}) filters on it) — always present, an empty array for an untagged asset. "
          "MODELS also carry their SIZE, as information (services/extentmeasure.h): `extent` {x,y,z} — the model's axis-aligned size in METRES as this asset was IMPORTED, i.e. after its import settings (scale, units, rotation) were baked in, which is the size every placement of it has; and `unitScale` — metres per source unit as the FILE declared it (FBX UnitScaleFactor/100, 1 for formats that declare none), which is what the import dialog shows so a user can disagree with the file. Nothing reads these to scale anything: an asset's size is decided once, at import (assets.importSettings / assets.reimport), and every instance is placed at scale 1. (The retired fit-to-size block — fitKind/fitScale/fitReason/fitSource, and the assets.setFit verb behind it — guessed a size from an envelope on every instantiation; a stale row may still carry those keys and nothing reads them.)",
          Needs::Document },
        { "meshLods", "assets.meshLods(guid) -> [{mesh, level, triangles, error, switchPixels}]",
          "The automatic LOD chain a MODEL asset's bake carries (ATOM stage 1, SPECS/NANITE_SPEC.md §7), one row per mesh per level, level 0 (the authored geometry) included. `error` is that level's simplifier error (position + attribute quadrics, >= the geometric error) as a LENGTH IN THE MODEL'S OWN UNITS — 0 for level 0 — and `switchPixels` is the SIZE ON SCREEN at which the renderer swaps to it at LOD bias 1: the projected radius of the mesh's bounding sphere, in pixels, at which that level's error covers one pixel. It is a size and not a distance because the renderer's rule is a real screen-space pixel error at whatever lens, window and render target the pass is using — the same asset switches at the same SIZE on a 4K window, in a 256-pixel thumbnail and in either eye of a headset, and therefore at very different distances. "
          "An EMPTY list is the honest answer for a model with no chain, and there are four ways to have none: the asset has no bake yet (assets.bakeAll builds them), the mesh is SKINNED (stage 1 ships static meshes only), it is too small to be worth simplifying, or its topology stopped the simplifier before it could shed a useful fraction. Nothing here is authored: the chain is built at import and the levels are derived, never stored as a user setting.",
          Needs::Document },
        { "meshCards", "assets.meshCards(guid) -> [{mesh, card, axis, origin, size, depth, lodLevel, texel, coverage, meshCoverage}]",
          "The SURFACE CARDS a MODEL asset's bake carries (SURFACE-CACHE phase 1, SPECS/SURFACE_CACHE_ASSESSMENT.md), one row per card per mesh. A card is an axis-aligned orthographic capture of a patch of the mesh's surface, in the MESH'S OWN SPACE: `axis` is one of '+X','-X','+Y','-Y','+Z','-Z' — the direction the card looks FROM; `origin` is the centre of its box; `size` is the capture rectangle {u, v} in metres and `depth` how deep along the axis it must see; `texel` is `max(size) / 128` — the metres per texel a 128-texel page would give it, and the number `lodLevel` was chosen by (the coarsest baked LOD level whose simplifier error is below that texel, the same rule the voxeliser picks a level by). "
          "`coverage` is the fraction of the mesh's sampled surfels THAT card sees — facing it, inside its rectangle and depth range, and not hidden behind nearer surface of the same mesh — and `meshCoverage` is the fraction covered by at least one card of the mesh, which is the quality of the whole list. Cards overlap, so the per-card numbers do not sum to it. "
          "Nothing reads a card yet: the capture is phase 2 and the hit lighting is phase 4. This verb is how the list they will spend is inspected. "
          "An EMPTY list is the honest answer for: a model with no bake yet (assets.bakeAll builds them), a SKINNED mesh (a card baked against a bind pose is a lie — the same limit Epic states), a mesh with no surface area, and an asset imported with `maxCards: 0`. The budget is `maxCards` on the import record (assets.importSettings / assets.import / assets.reimport), 12 by default.",
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
        { "import", "assets.import(path, {units, scale, axes, rotate, translate, skeleton, clips, materials, maxCards}) -> guid",
          "Imports a mesh file (obj, fbx, dae, glb, gltf, ply, stl — Constants::MODEL_EXTS) into the global asset store. NOT undoable. "
          "THE TYPE FOLLOWS THE FILE, not the extension: a model file that carries animation and NO geometry — a Mixamo download 'without skin', a .bvh capture — is stored as an ANIMATION asset (its own library type; every mesh path refuses a zero-mesh file), while a file with meshes stays an object even when it also carries clips. "
          "Read the type back with assets.metadata(guid).kind or assets.list({type: 'animation'}). "
          "THE OPTIONS ARE THE IMPORT SETTINGS (SPECS/IMPORT_DIALOG_SPEC.md): what the import dialog asks, as a verb. They are BAKED INTO THE ASSET — the geometry, the skeleton and the clips are transformed once, at import — so every instance of the asset is placed at scale 1 and nothing rescales it later. "
          "`units` ('auto' | 'm' | 'cm' | 'mm' | 'in' | 'ft'): 'auto' trusts the file's own declaration (an FBX UnitScaleFactor; every other format at this pin declares nothing and means metres), anything else OVERRIDES it — 'cm' on a file that wrongly says metres means 'treat its numbers as centimetres'. "
          "`scale`: a positive uniform multiplier on top of the unit. "
          "`axes` {up, forward}: the axes THE FILE uses, as signed names ('+X'..'-Z'); our convention is up '+Y', forward '-Z', which is the identity. They must be perpendicular. "
          "`rotate` [x,y,z]: a free rotation in degrees, applied after the axis fix. `translate` [x,y,z]: an origin shift in METRES, applied last. "
          "`skeleton`, `clips` (true | false | a list of clip names) and `materials` ('import' | 'none') are recorded and are part of the asset's bake key; the pipeline builds all of them today. "
          "`maxCards` (0..64, default 12 — Epic's own 'Max Lumen Mesh Cards') is how many SURFACE CARDS the bake may author per mesh: axis-aligned orthographic captures of the surface, read back with assets.meshCards. 0 authors none. It is part of the bake key too, so changing it re-bakes the asset rather than quietly leaving the old list in place. "
          "An unknown key or a refused value FAILS the import rather than importing at the wrong size. Read the record back with assets.importSettings(guid) and change it with assets.reimport(guid, {...}). "
          "AN IMPORT YOU ASK FOR IS YOURS (the rule assets.importFile states in full): importing bytes the library already holds as a MATERIAL'S MEMBER texture answers with that row, unstamped, instead of a second copy. Models are never stamped, so for this verb it is the rule of the pipeline rather than a thing that happens here. "
          "AND WHAT IT IS NOT (SEED-SMALL-2, stated so nobody reads it as a bug): the answer-by-content rule covers a MATERIAL'S MEMBER picture only. Importing the same file twice yourself mints a SECOND library row over the one stored object — the bytes are content-addressed and never copied, but two rows means two tiles with two names, two thumbnails and two sets of tags, and nothing merges them afterwards. That is the long-standing behaviour of this pipeline, on purpose for now: whether a user's own second import of their own picture should answer with the first row is an open question for the owner (IMPORT-DEDUP-1), not an oversight here.",
          Needs::Document },
        { "importFile", "assets.importFile(path, drawerId?, {typeHint, units, scale, axes, rotate, translate, skeleton, clips, materials, maxCards}) -> guid",
          "Imports any library-supported file (models, animation clips, images, audio, video) into the asset store, optionally filed in a drawer. Images/audio/video are headless-safe (video decodes through Qt Multimedia's ffmpeg backend, no display needed). NOT undoable. "
          "`typeHint` overrides the pipeline's SNIFF with an asset type name (the assets.list vocabulary: object, animation, texture, music, video, file, ...) — for the file whose extension lies, or the one the sniffer will not claim. It is a HINT to the importer selection, not a relabel of the result: a hint the pipeline cannot honour fails rather than filing bytes under the wrong kind. Unknown names are refused with the list. "
          "RE-IMPORTING A MATERIAL'S MEMBER PICTURE (MATERIAL_BUNDLE_SPEC V-2): a texture that arrived inside a material — picked through its picker, or shipped with a preset — is stamped as that bundle's member and folds into its tile, so the user never sees it on its own. Importing those exact bytes YOURSELF answers with THAT row and clears the stamp: the same guid comes back, it is a tile from then on (assets.list({scope:'store'}) and the editor tray list it), and the material keeps it as a member — membership is the material's definition, never the stamp. One picture stays one row and one stored object; nothing is copied. An import a MATERIAL asks for never clears a stamp, and a deleted-but-pinned row is still re-listed rather than duplicated. "
          "THAT RULE IS THE NARROW ONE, AND THIS IS ITS EDGE (SEED-SMALL-2): it is about a STAMPED member picture. A file the user imported themselves carries no stamp, so importing it a second time mints a SECOND row over the same stored object — one object on disk, two catalog rows, two tiles, each with its own name, thumbnail and tags, and nothing merges them later. Long-standing and deliberate for now; widening the answer-by-content rule to any listed top-level row on the same bytes is an open decision (IMPORT-DEDUP-1), so a second tile after a second import is this verb behaving as documented rather than a defect.",
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
        { "folders", "assets.folders({parent}) -> [{guid, name, parent, count}]",
          "THE OPEN PROJECT'S FOLDERS — the tray's own organisation (DRAWERS-1). A FOLDER IS NOT A DRAWER: "
          "a drawer (assets.drawers, a numbered `collections` row) organises the LIBRARY and is the same for "
          "every project; a folder organises ONE project's asset tray and is named by guid. With no `parent` "
          "this is every folder the project has; with one (a folder guid, or the project's own guid for the "
          "root) it is that folder's children only. `count` is how many ROWS the folder holds — the UNION of the rows "
          "the project owns and the library assets it pins there, never their sum: a row is very "
          "often both (an import made with a project open writes the project's guid on the row "
          "and addToProject then pins it) and the two filings describe ONE asset. Not a recursive "
          "total, and not the collapsed TILE count: a row the tray folds into another still "
          "counts as filed here. "
          "The editor's own hidden folder (Systems, which holds a row per particle emitter) is listed "
          "like any other: it is a real row, and the verbs refuse to rename, move or delete it "
          "because the editor finds it BY NAME.",
          Needs::Document },
        { "createFolder", "assets.createFolder(name, {parent}) -> guid",
          "Creates a folder in the open project and returns its guid — the editor tray's right-click > Create > "
          "New Folder, as a verb. `parent` is a folder guid (default: the project root). "
          "Refuses an empty name, a duplicate name under the same parent, an unknown parent, and the name the "
          "editor keeps for itself (Systems — it resolves that one by name, so a second one would hijack it). UNDOABLE: one Ctrl+Z removes it, and anything filed into it meanwhile moves up to its parent "
          "rather than leaving the project.",
          Needs::Document },
        { "renameFolder", "assets.renameFolder(guid, name) -> bool",
          "Renames a project folder. Refuses an empty name, a duplicate under the same parent, an unknown "
          "folder, a folder belonging to another project, and the editor's own Systems. NOT undoable.",
          Needs::Document },
        { "deleteFolder", "assets.deleteFolder(guid, {keepContents}) -> bool",
          "Deletes a project folder. `keepContents` defaults to TRUE: everything inside — child folders and "
          "filed rows alike — moves up to the deleted folder's parent, so tidying up can never lose an asset. "
          "With `keepContents: false` the folder's rows LEAVE THE PROJECT instead (the project's pin and the "
          "members only it uses — the same door assets.removeFromProject uses; the LIBRARY row is never "
          "touched) and its subtree of folders goes with them. Refuses the project root, an unknown folder and "
          "the editor's own Systems (and a folder that HOLDS one, which cannot go without it). NOT undoable.",
          Needs::Document },
        { "moveToFolder", "assets.moveToFolder(guidOrGuids, folderGuid | null) -> n",
          "Files one row or many in a project folder — the tray's drag of a (multi-)selection onto a folder "
          "tile — and answers how many rows actually moved. `folderGuid` null, empty or the project's own guid "
          "means the ROOT. A FOLDER GUID may be passed as a row: the folder itself moves (a move into its own "
          "subtree, and any move of the editor's own Systems folder, is refused). "
          "Filing changes where a row is LISTED IN THIS PROJECT and nothing else: no pin moves, no content is "
          "re-resolved, no other project sees it — which is why a pinned library asset's folder is recorded on "
          "the PIN (a library row is shared, so writing the folder on the row would file it for everybody). "
          "EVERY ROW IS JUDGED BEFORE ANY ROW MOVES, so a refusal moves nothing: an unknown guid, a row this "
          "project neither owns nor pins, and an import MEMBER (a mesh or a texture that arrived inside a "
          "model — it is part of that asset, not a tile in a folder) are all refused. UNDOABLE AS ONE STEP, "
          "however many rows it carries.",
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
        { "addToScene", "assets.addToScene(guid, {position, onSurface}) -> nodeId",
          "Instantiates a project object asset into the scene (undoable, like a drag from the asset "
          "browser). `position` places the node's PIVOT there; `onSurface: true` treats that point "
          "as a SURFACE and rests the model's bounding box on it instead — which is what a drag "
          "from the asset browser does, since a model pivoted at its centre otherwise lands half "
          "inside the floor (owner, 2026-09-14); it needs a `position` and is refused without one. "
          "A model already pivoted at its base is not lifted, "
          "so nothing is lifted twice.",
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
          "companion PBR material that ADDING IT MINTED — recognised by the stamp the mint writes, "
          "never by its shape, so a material you authored on the same image is never touched — as "
          "long as this project pins it, nothing else depends on it (an object it is applied to keeps "
          "it) and it has not since been given a second map. "
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
        { "refreshThumbnail", "assets.refreshThumbnail(guid) -> {ok, reason}",
          "Rebuilds an asset's thumbnail synchronously and writes it to the database. Objects, particle systems, materials, shader graphs and AVATARS render on the engine (engine required; a shader renders the material its graph evaluates to, on the preview sphere; an avatar renders its own character model); images re-thumbnail from the source file, videos re-grab a first-second frame, animation clips redraw their pose strip, and audio/file rows reset to their type icon (document-only). `ok` is false with `reason` naming WHY nothing was stored — a thumbnail that fails is never silent.",
          Needs::Document },
        { "rebuildThumbnails", "assets.rebuildThumbnails({missingOnly, projectOnly, limit}) -> {considered, rebuilt, skipped, cancelled, failed: [{guid, reason}]}",
          "Rebuilds thumbnails in bulk — the repair pass for rows that are already grey. `missingOnly` (default true) takes only the rows whose stored thumbnail is absent or undecodable; false redraws every asset that has a thumbnail to draw. `projectOnly` (default false) limits it to the open project's pinned assets; `limit` (default 0 = no limit) caps how many are rebuilt. One asset per turn, yielding between them, so the window keeps painting. `skipped` counts the rows with nothing to draw at all (a builtin primitive's row — the default Ground — stores no model definition), which are not failures; a row whose stored bytes are GONE is a failure with its reason. `cancelled` is true when the sweep was stopped before it finished — the app is quitting, or the script was stopped — and whatever it had already rebuilt is stored. Each asset goes through the same routine as assets.refreshThumbnail.",
          Needs::Document },
        { "thumbnail", "assets.thumbnail(guid) -> {guid, empty, bytes, width, height, centre: {r, g, b}, coverage}",
          "The thumbnail stored for an asset, as facts rather than pixels: byte size of the PNG blob, its decoded dimensions, the colour of its centre pixel (0-255) and `coverage` — the fraction of the image (0..1) that differs from the background the renderer cleared to, i.e. how much of the tile the subject fills. empty is true when the row carries no image. Document-only — it reads the database, it does not render.",
          Needs::Document },
        { "select", "assets.select(guid) -> bool",
          "Selects an asset in the ASSETS PAGE: the tile becomes current, is scrolled into view and its preview loads — exactly what a double-click on the tile does. This is what an import does with the asset it just made (the tile used to appear unselected and had to be hunted for). False when the page has no tile for that guid (a guid the current drawer/filter hides, or a session with no window).",
          Needs::Window },
        { "selected", "assets.selected() -> guid | ''",
          "The guid of the Assets page's current tile, empty when nothing is selected.",
          Needs::Window },
        { "preview", "assets.preview() -> {guid, subject, bounds: {min, max, size, center}, camera: {position, pivot, distance}}",
          "What the Assets page's preview viewer is showing: the selected asset's guid, the previewed node's name, its WORLD BOUNDS (the size the preview renders, which is the size the editor places — the fit-to-size factor is applied on both paths) and where the orbit camera stands. `bounds` is absent when nothing with geometry is previewed. The verb a test uses to prove a preview and a drop agree.",
          Needs::Window },
        { "fly", "assets.fly({x, y, z} | {forward, right, up}) -> bool",
          "Flies the Assets preview camera, camera-relative, in metres: `forward`/`right`/`up` are along the camera's own axes (the arrow/WASD keys' directions, the editor's fly step), `x`/`y`/`z` are a world-space offset. Both forms move the orbit pivot, so the arcball survives the flight. False when no asset preview is up.",
          Needs::Window },
        { "exportRaw", "assets.exportRaw(guid, dir, {dependencies: true, hash: true}) -> {dir, manifest, files, assets, totalBytes, warnings}",
          "Exports a store asset's files (and, by default, its dependencies' files) as loose files with their original names into dir, plus a jah.manifest.json (manifest v2: guids, types, dependency edges, sizes, sha256 content ids — hashing skippable via {hash: false}). Identical bytes are written once; assets with no stored files still get manifest entries. The unified-export front half (ASSET_PIPELINE_SPEC §3.3); .jaf export joins it in the final half.",
          Needs::Document },
        // THE NAME. The spec asks for `assets.export`; `export` is a C++
        // keyword, and this registry dispatches a verb STRICTLY by the
        // invokable method's own name (scriptworker.cpp) — so the verb is
        // `exportBundle`, beside the `exportRaw` it belongs with, and the
        // alternative (an alias mechanism in the API core, for one name) buys
        // nothing a reader of the docs would notice.
        { "exportBundle", "assets.exportBundle(guid, path) -> {path, kind, assets, bytes}",
          "Writes ONE asset and everything it is made of as a self-contained share file (a .jbundle zip: "
          "manifest v2 plus the closure with its bytes inline). A material carries its textures and its baked "
          "maps, so it opens on a machine that has never seen any of them — the owner's 'material bundles are "
          "great if they are self-contained'. The version that travels is the one the OPEN PROJECT renders "
          "with when it holds the asset, the library's otherwise. Read it back with assets.import(path).",
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
        { "storeStatus", "assets.storeStatus() -> {root, online, missing, deviceWaits}",
          "Store reachability: the active root, whether it is reachable (offline mode keeps the catalog fully usable), and how many library rows have no folder under it. `deviceWaits` is how many times THIS PROCESS has waited for the storage device — one per fsync the store performed: the two-phase ingest's flush, a cross-filesystem ingest's copy fallback, and the store-root move's per-file copy. A hardlinked object writes no bytes and waits for nothing, so it does not count. Counted where the wait happens, never inferred. It is monotonic and process-wide, so only differences mean anything; bracket a gesture with two reads to assert that it cost no durable write, which is how \"no fsync on the thread that draws\" (FSYNC-2) stops being a claim and becomes a test.",
          Needs::Document },
        { "importSettings", "assets.importSettings(guid) -> {name, sourceName, sourceOid, importer, importerVersion, assimp, settings, defaults}",
          "The determinism record the ONE import pipeline stamped on the asset: content id of the source, "
          "importer name/version, assimp version, and `settings` — the COMPLETE import-settings record "
          "this asset was imported under (assets.import documents every key). A row imported before the "
          "import dialog carries no settings and reads back as the defaults, which is exactly what was "
          "applied to it. "
          "`name` is the row's display name and `sourceName` the stored source file's, whose EXTENSION "
          "is what a reader of the stored bytes needs (the content store names its objects by hash). "
          "`defaults` is what the FILE ITSELF says, for a dialog or a test that wants to show the user "
          "what they are overriding: `declaredUnitScale` (metres per source unit as the file declares — "
          "FBX UnitScaleFactor/100, 1 for the formats that declare none) and `extent` {x,y,z}, the "
          "measured size in metres AS IMPORTED. Absent for a non-model asset.",
          Needs::Document },
        { "reimport", "assets.reimport(guid, {units, scale, axes, rotate, translate, skeleton, clips, materials, maxCards}) -> {guid, settings, extent, bakeOid}",
          "RE-READS a model asset's stored SOURCE with new import settings and rebuilds everything derived "
          "from it: the mesh bake (the LOD chain and the SURFACE CARDS with it), the measured extent, the "
          "recorded settings and the thumbnail. The options are MERGED over the stored record, so a caller sends only what it is "
          "changing. "
          "A reimport is NOT a new import: the source content id, the row's guid, its member Texture rows "
          "and every project's pin are unchanged, because a pin freezes source BYTES and those bytes did "
          "not move. The old bake drops to no references and `assets.gc` reaps it. Meshes already placed in "
          "the OPEN scene are swapped to the new geometry in place; their node transforms are untouched, "
          "which is right because every placement is at scale 1. "
          "The swap is NOT UNDOABLE and is outside the undo stack, like every asset mutation: undoing "
          "a structural edit made before the reimport restores a node holding the OLD mesh, which "
          "the next reimport or reopen replaces. "
          "ONE EXCEPTION, stated because it cannot be migrated: a scene saved BEFORE the fit-to-size policy "
          "retired holds that fit as a REAL node scale, so such a scene renders the old fit multiplied by "
          "the new bake. Ships-as-new-app — re-place the node. "
          "Refuses a non-model row, a row with no stored source, and the same unknown keys and bad values "
          "assets.import refuses. NOT undoable — asset mutations never are (SCRIPTING_SPEC §1.6.5).",
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
        { "restampSeed", "assets.restampSeed() -> {stamped, presets, skipped, scanned, ran}",
          "Gives the SHIPPED PRESETS\u2019 MAPS the member stamp in a library that already exists "
          "(SEED-RESTAMP-1). A preset\u2019s picture arrived inside a material, so it folds into the "
          "bundle\u2019s tile instead of standing in the tray as one of the user\u2019s own "
          "(MATERIAL_BUNDLE_SPEC V-2) \u2014 but the seed writes that stamp as it MINTS each row, so a "
          "library seeded by a build older than the stamping seed keeps its maps loose for ever (the seed "
          "is idempotent and never looks at them again). This is that one repair, and the app runs it "
          "itself at every launch beside the seed. "
          "MATCHED BY CONTENT, NEVER BY NAME: a texture row is a preset\u2019s map when its stored source "
          "object is one a shipped preset\u2019s own definition names \u2014 so a duplicate row over the "
          "same bytes is repaired too, and a picture the user imported themselves is untouched. A preset "
          "with at least one already-stamped map is left alone entirely: a stamping seed minted that batch, "
          "so an unstamped map of it is the user\u2019s own copy. "
          "`stamped` is how many rows were repaired, `presets` across how many bundles, `skipped` how many "
          "presets were left alone, `scanned` how many texture rows were looked at, `ms` what the pass cost "
          "the thread that called it, and `ran` is false when "
          "the cheap pre-gate answered (every texture row already carries a stamp). IDEMPOTENT: a second "
          "call writes nothing. Nothing is pinned, unpinned, renamed or moved, and no bytes are touched. "
          "NOT undoable \u2014 asset mutations never are (SCRIPTING_SPEC \u00a71.6.5).",
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
    // THE TRAY LISTING (S9, L13): the editor tray's own listing, from the one
    // function the panel calls (services/assettray.h) — every asset the
    // project's scene uses, once. A PROJECT's listing: refused for the other
    // scopes.
    const bool trayOnly = options.value("tray", false).toBool();
    // "SHOW MEMBER TEXTURES" (MATERIAL_BUNDLE_SPEC V-2, owner Q4): the pictures
    // that arrived inside a material bundle fold into it by default. The same
    // switch the panels carry, so the verb and the windows list the same rows.
    const bool showMembers = options.value("members", false).toBool();
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
        if (trayOnly) {
            fail("assets.list: scope 'session' has no catalog rows to collapse — the tray "
                 "listing is a rule over library/project rows. List scope 'store'/'project'");
            return out;
        }
        // The live AssetManager registrations — what the viewport's drag-drop
        // lookups and the panels actually see. Makes session hydration
        // observable to scripts and tests (IMAGE_PLANE_SPEC §6 gate). They
        // belong to the OPEN project; with none open there are none.
        if (host.project && !host.project->getProjectGuid().isEmpty()) {
            for (Asset *asset : AssetManager::getAssets()) {
                if (!asset) continue;
                if (typeFilter >= 0 && static_cast<int>(asset->type) != typeFilter) continue;
                if (!nameMatches(asset->fileName)) continue;
                if (full()) break;
                out.append(QVariantMap{ { "guid", asset->assetGuid },
                                        { "name", asset->fileName },
                                        { "type", typeName(static_cast<int>(asset->type)) } });
            }
        }
        // LIVE TEXTURES are session objects that outlive every project, so they
        // are listed straight from their catalog — never mirrored into
        // AssetManager, whose list dies at every project.open (code review
        // 2026-09-10: the mirror went stale and needed a project to be seen).
        const int liveType = static_cast<int>(ModelTypes::LiveTexture);
        if (typeFilter < 0 || typeFilter == liveType) {
            for (const auto &r : LiveTextureCatalog::list()) {
                if (!nameMatches(r.name)) continue;
                if (full()) break;
                out.append(QVariantMap{ { "guid", r.guid }, { "name", r.name },
                                        { "type", typeName(liveType) } });
            }
        }
        return out;
    }
    if (scope == "store") {
        if (trayOnly) {
            fail("assets.list: the tray listing is a PROJECT's — the editor tray shows what the "
                 "open project uses. List scope 'project' with tray: true");
            return out;
        }
        // THE LIBRARY LISTING through the one function the Assets page reads
        // (ASSETS-PAGE-MEMBERS-1): a legacy Shader row and, unless `members`,
        // a picture that arrived inside a material bundle are not tiles.
        records = assettray::libraryList(host.db, showMembers);
    } else if (scope == "project") {
        if (!requireProject()) return out;
        if (trayOnly) {
            // The root of the tray, exactly as the panel lists it (a type
            // filter keeps one type of the same listing, as the panel's filter
            // combo does — it does not sweep the editor's hidden folders).
            const QString projectGuid = host.project->getProjectGuid();
            records = assettray::list(host.db, projectGuid, projectGuid, typeFilter, showMembers);
        } else if (typeFilter >= 0) {
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
        } else {
            records = host.db->fetchChildAssets(host.project->getProjectGuid(),
                                                host.project->getProjectGuid(), -1);
            // Reference-with-pin (phase 4): pinned LIBRARY assets are project
            // members too — a project "use" is a project_assets row, not a
            // cloned Editor row. fetchProjectPinnedAssets is the shared source
            // (the editor's asset tray reads the same rows).
            for (const auto &record :
                 host.db->fetchProjectPinnedAssets(host.project->getProjectGuid())) {
                if (std::any_of(records.begin(), records.end(),
                                [&](const AssetRecord &r) { return r.guid == record.guid; }))
                    continue;
                records.append(record);
            }
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

QVariantList AssetsApi::meshLods(const QString &guid)
{
    QVariantList out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        fail(QStringLiteral("assets.meshLods: no asset with guid '%1'").arg(guid));
        return out;
    }
    const QString source = storeFileFor(guid);
    if (source.isEmpty()) {
        fail(QStringLiteral("assets.meshLods: '%1' has no stored source file").arg(guid));
        return out;
    }
    // The BAKE is where the chain lives — reading it is also the only honest
    // answer to "what would the renderer get", since a model with no fresh bake
    // is parsed at open and gets no chain at all. BY THIS ROW (the second
    // read's F9): a path alone resolves that content's DEFAULT settings
    // variant, which is a different bake for an asset with its own settings.
    iris::BakedModelPtr baked = MeshBakeStore::load(source, guid);
    if (!baked) return out;   // no usable bake: an empty list, not an error
    for (int m = 0; m < baked->meshes.size(); ++m) {
        const iris::MeshPtr &mesh = baked->meshes.at(m);
        if (mesh.isNull()) continue;
        const iris::IndexBufferPtr ib = mesh->getIndexBuffer();
        const int baseTriangles = (ib && ib->dataSize > 0)
                                      ? ib->dataSize / int(sizeof(unsigned) * 3) : 0;
        QVariantMap row;
        row["mesh"] = m;
        row["level"] = 0;
        row["triangles"] = baseTriangles;
        row["error"] = 0.0;
        row["switchPixels"] = 0.0;
        out.append(row);
        const int levels = std::min(mesh->lodIndices.size(), mesh->lodErrors.size());
        for (int i = 0; i < levels; ++i) {
            QVariantMap lod;
            lod["mesh"] = m;
            lod["level"] = i + 1;
            lod["triangles"] = int(mesh->lodIndices.at(i).size() / 3);
            lod["error"] = double(mesh->lodErrors.at(i));
            // THE SCREEN SIZE, not a distance (ATOM-3 A1): the renderer's rule
            // is a PIXEL budget at the live lens and viewport, so a level has
            // no fixed switch distance any more — it has a size on screen.
            // `budget * radius / error` is the projected RADIUS of the mesh's
            // bounding sphere, in pixels, at which its error covers the budget
            // (exact in the far field, where `distance - radius ~ distance`);
            // below that size the renderer takes this level, on any lens, at
            // any resolution, in either eye of a headset.
            const float r = mesh->getBoundingSphere().radius;
            const float e = mesh->lodErrors.at(i);
            lod["switchPixels"] = (r > 0.0f && e > 0.0f)
                                      ? double(jahshaka::engine::kLodBudgetPixels * r / e)
                                      : 0.0;
            out.append(lod);
        }
    }
    return out;
}

QVariantList AssetsApi::meshCards(const QString &guid)
{
    QVariantList out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        fail(QStringLiteral("assets.meshCards: no asset with guid '%1'").arg(guid));
        return out;
    }
    const QString source = storeFileFor(guid);
    if (source.isEmpty()) {
        fail(QStringLiteral("assets.meshCards: '%1' has no stored source file").arg(guid));
        return out;
    }
    // Same read as assets.meshLods and for the same reason: the BAKE is where
    // the card list lives, and a model with no fresh bake gets none at open.
    iris::BakedModelPtr baked = MeshBakeStore::load(source, guid);
    if (!baked) return out;   // no usable bake: an empty list, not an error
    static const char *kAxisNames[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
    for (int m = 0; m < baked->meshes.size(); ++m) {
        const iris::MeshPtr &mesh = baked->meshes.at(m);
        if (mesh.isNull()) continue;
        for (int c = 0; c < mesh->cards.size(); ++c) {
            const iris::MeshCard &card = mesh->cards.at(c);
            QVariantMap row;
            row["mesh"] = m;
            row["card"] = c;
            row["axis"] = QString::fromLatin1(
                kAxisNames[std::min<int>(card.axis, iris::MeshCard::kAxisCount - 1)]);
            row["origin"] = QVariantList{ double(card.origin.x()), double(card.origin.y()),
                                          double(card.origin.z()) };
            // SIZES, not half-sizes: a user reading this wants "how big is the
            // capture", and the half-extent form belongs to the maths that
            // places a camera.
            row["size"] = QVariantList{ double(card.halfU * 2.0f), double(card.halfV * 2.0f) };
            row["depth"] = double(card.halfDepth * 2.0f);
            row["lodLevel"] = int(card.lodLevel);
            row["texel"] = double(std::max(card.halfU, card.halfV) * 2.0f
                                  / float(iris::MeshBake::cardCaptureResolution()));
            row["coverage"] = double(card.coverage);
            row["meshCoverage"] = double(mesh->cardCoverage);
            out.append(row);
        }
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
    // THE COMPANION STAMP (R10.4, owner review 2026-09-18). "Create material
    // from image" mints a Material row for one Texture and stamps it
    // `companionOf: <texture guid>` — services/imagematerial.cpp
    // createMaterialAsset is the ONLY writer of it, and the asset tray's fold
    // rule is the only reader. It was invisible from the verb surface, so a
    // script (or a person asking why their image tile vanished) could not see
    // that the two rows belong together. Reported only when it is there: a row
    // nobody minted carries no key rather than an empty string.
    // (`record.asset` is the row's definition. It was EMPTY here until the
    // SMALL-UI-A fix round: fetchAsset was the one query that did not select
    // the column, so the same field meant "the definition" from some readers
    // and "" from this one. Fixed at the query; this reads the record it
    // already has instead of asking a second time.)
    if (!record.asset.isEmpty()) {
        const QString companion =
            QJsonDocument::fromJson(record.asset).object()
                .value(QStringLiteral("companionOf")).toString();
        if (!companion.isEmpty()) out["companionOf"] = companion;
    }
    // THE MEMBER STAMP (MATERIAL_BUNDLE_SPEC V-2), the other half of the same
    // question and invisible from the verb surface until RESET-LIBRARY-1: a
    // picture that arrived INSIDE a material — through the picker, or with a
    // shipped preset's seed — carries `member: true` and `memberOf: <the
    // material it came in through>`, and that stamp is what folds its tile
    // into the bundle's while only materials use it. Reported only when it is
    // there, like `companionOf`: a row the user imported themselves carries no
    // key rather than a false.
    if (memberstamp::isStamped(record.properties)) {
        out["member"] = true;
        const QString origin = memberstamp::originOf(record.properties);
        if (!origin.isEmpty()) out["memberOf"] = origin;
    }
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

bool AssetsApi::rename(const QString &guid, const QString &name)
{
    if (!host.db) return fail("assets: not available in this session");
    const QString wanted = name.trimmed();
    if (wanted.isEmpty())
        return fail("assets.rename: a name is required (renaming to '' would leave a row nothing "
                    "can be found by)");
    if (host.db->fetchAsset(guid).guid.isEmpty())
        return fail(QStringLiteral("assets.rename: no asset with guid '%1'").arg(guid));
    // THE REFUSAL IS ENFORCED IN assettags::write (the one place both this
    // verb and the Assets page's Update button write a name); this says WHY,
    // in the sentence the definition writer already uses for the same law.
    const QString shipped = MaterialBundle::shippedPresetName(guid);
    if (!shipped.isEmpty() && wanted != shipped)
        return fail(QStringLiteral("assets.rename: '%1' is a material the app ships and is "
                                   "read-only - materials.edit gives you this project's own copy "
                                   "of it, and that one renames").arg(shipped));
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

QString AssetsApi::import(const QString &path, const QVariantMap &options)
{
    if (!host.services || !host.services->assets) { fail("assets: not available in this session"); return QString(); }
    QJsonObject settings;
    QString settingsError;
    if (!importSettingsFromOptions(QStringLiteral("assets.import"), options, &settings,
                                   &settingsError)) {
        fail(settingsError);
        return QString();
    }
    // A SHARE FILE IS AN IMPORT LIKE ANY OTHER (MATERIAL_BUNDLE_SPEC Q5): one
    // asset plus its closure, written by assets.export. It does not go through
    // the model importers — it carries catalog ROWS, not a file to convert —
    // so it is answered here, before the model gate below refuses its
    // extension.
    if (assetshare::looksLikeBundle(path)) {
        if (!options.isEmpty()) {
            fail("assets.import: a share file carries its own import settings; pass no options");
            return QString();
        }
        const auto landed = assetshare::importBundle(host.db, host.project, path);
        if (!landed.ok()) {
            fail(QStringLiteral("assets.import: %1").arg(landed.error));
            return QString();
        }
        return landed.guid;
    }

    const auto result = host.services->assets->importMesh(path, settings);
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
    static const QStringList knownOptions = {
        QStringLiteral("typeHint"), QStringLiteral("version"), QStringLiteral("units"),
        QStringLiteral("scale"),    QStringLiteral("axes"),    QStringLiteral("rotate"),
        QStringLiteral("translate"),QStringLiteral("skeleton"),QStringLiteral("clips"),
        QStringLiteral("materials") };
    const QString refusal = refuseUnknownKeys(QStringLiteral("assets.importFile"), options,
                                              knownOptions);
    if (!refusal.isEmpty()) { fail(refusal); return QString(); }
    QVariantMap settingsOptions = options;
    settingsOptions.remove(QStringLiteral("typeHint"));
    QJsonObject settings;
    QString settingsError;
    if (!importSettingsFromOptions(QStringLiteral("assets.importFile"), settingsOptions,
                                   &settings, &settingsError)) {
        fail(settingsError);
        return QString();
    }
    // ONLY A MODEL CARRIES THEM (the second read's F8). The record is a
    // complete model-import recipe, and stamping one onto every image, sound
    // and video row would say something false about those assets — and about
    // a bake that will never exist for them. A caller that passes a key for a
    // media file is refused above; one that passes none gets none.
    if (!Constants::MODEL_EXTS.contains(QFileInfo(path).suffix().toLower()))
        settings = QJsonObject();
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
    const auto result = host.services->assets->importFile(path, drawerId, typeHint, settings);
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

// --- THE OPEN PROJECT'S FOLDERS (DRAWERS-1) --------------------------------
//
// Every one of these is a thin view over services/projectfolders.h, which is
// the ONE folder model the editor tray and the Materials module's project
// drawer are both views of. The rules and their wording live there, so a
// refusal reads the same in a script, in a toast and in a test.

QVariantList AssetsApi::folders(const QVariantMap &options)
{
    QVariantList out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    if (!requireProject()) return out;
    for (const QString &key : options.keys())
        if (key != QLatin1String("parent")) {
            fail(QStringLiteral("assets.folders: unknown option '%1' (parent)").arg(key));
            return out;
        }
    const QString projectGuid = host.project->getProjectGuid();
    for (const auto &folder : projectfolders::list(host.db, projectGuid,
                                                   options.value("parent").toString()))
        out.append(QVariantMap{ { "guid", folder.guid },
                                { "name", folder.name },
                                { "parent", folder.parent == projectGuid ? QString()
                                                                         : folder.parent },
                                { "count", folder.count } });
    return out;
}

QString AssetsApi::createFolder(const QString &name, const QVariantMap &options)
{
    if (!host.db) { fail("assets: not available in this session"); return QString(); }
    if (!requireProject()) return QString();
    for (const QString &key : options.keys())
        if (key != QLatin1String("parent")) {
            fail(QStringLiteral("assets.createFolder: unknown option '%1' (parent)").arg(key));
            return QString();
        }
    const QString projectGuid = host.project->getProjectGuid();
    const QString parent = options.value("parent").toString();

    // UNDOABLE, on the session's stack when there is one. The command mints
    // the guid at construction, so the caller has it whether or not the create
    // went through (and a redo re-creates the same folder).
    // JUDGED BEFORE IT IS PUSHED. A command whose redo() refuses still sits on
    // the stack as a step that does nothing — one Ctrl+Z spent on a folder
    // that was never created — so the answer is taken from the model FIRST and
    // nothing is pushed unless the create will happen.
    const projectfolders::Result judged =
        projectfolders::judgeCreate(host.db, projectGuid, name, parent);
    if (!judged.ok) {
        fail(QStringLiteral("assets.createFolder: %1").arg(judged.error));
        return QString();
    }

    if (host.undoStack) {
        // The outcome is read out of a box the CALLER owns: a command that
        // turns out to be a no-op deletes itself inside push().
        auto outcome = std::make_shared<FolderCommandOutcome>();
        host.undoStack->push(new CreateProjectFolderCommand(host.db, projectGuid, name,
                                                            parent, outcome));
        if (!outcome->error.isEmpty()) {        // lost a race with another writer
            fail(QStringLiteral("assets.createFolder: %1").arg(outcome->error));
            return QString();
        }
        return outcome->guid;
    }

    const projectfolders::Result result =
        projectfolders::create(host.db, projectGuid, name, parent);
    if (!result.ok) {
        fail(QStringLiteral("assets.createFolder: %1").arg(result.error));
        return QString();
    }
    return result.guid;
}

bool AssetsApi::renameFolder(const QString &guid, const QString &name)
{
    if (!host.db) return fail("assets: not available in this session");
    if (!requireProject()) return false;
    const projectfolders::Result result =
        projectfolders::rename(host.db, host.project->getProjectGuid(), guid, name);
    if (!result.ok) return fail(QStringLiteral("assets.renameFolder: %1").arg(result.error));
    return true;
}

bool AssetsApi::deleteFolder(const QString &guid, const QVariantMap &options)
{
    if (!host.db) return fail("assets: not available in this session");
    if (!requireProject()) return false;
    for (const QString &key : options.keys())
        if (key != QLatin1String("keepContents"))
            return fail(QStringLiteral("assets.deleteFolder: unknown option '%1' "
                                       "(keepContents)").arg(key));
    const bool keep = options.contains("keepContents") ? options.value("keepContents").toBool()
                                                       : true;
    const projectfolders::Result result =
        projectfolders::remove(host.db, host.project->getProjectGuid(), guid, keep);
    if (!result.ok) return fail(QStringLiteral("assets.deleteFolder: %1").arg(result.error));
    return true;
}

int AssetsApi::moveToFolder(const QVariant &guidOrGuids, const QVariant &folderGuid)
{
    if (!host.db) { fail("assets: not available in this session"); return 0; }
    if (!requireProject()) return 0;

    // ONE GUID OR MANY. A QVariantList arrives from a JS array; `Array.isArray`
    // is false for the bridged value, so the shape is read off the variant.
    QStringList guids;
    if (guidOrGuids.typeId() == QMetaType::QVariantList) {
        for (const QVariant &value : guidOrGuids.toList()) guids << value.toString();
    } else {
        guids << guidOrGuids.toString();
    }
    if (guids.isEmpty()) {
        fail("assets.moveToFolder: a guid (or an array of guids) is required");
        return 0;
    }
    // null / undefined / "" all mean THE ROOT.
    const QString target = folderGuid.isValid() && !folderGuid.isNull()
                               ? folderGuid.toString() : QString();
    const QString projectGuid = host.project->getProjectGuid();

    // JUDGED BEFORE IT IS PUSHED, for two reasons: a refused move must not
    // leave a no-op step on the user's stack, and neither must a move whose
    // rows are ALL already in that folder (a drop on the folder they came
    // from). `judgeMove` answers with the same rules and the same words.
    const projectfolders::Result judged =
        projectfolders::judgeMove(host.db, projectGuid, guids, target);
    if (!judged.ok) {
        fail(QStringLiteral("assets.moveToFolder: %1").arg(judged.error));
        return 0;
    }
    if (judged.moved == 0) return 0;            // nothing to do, and nothing to undo

    if (host.undoStack) {
        auto outcome = std::make_shared<FolderCommandOutcome>();
        host.undoStack->push(new MoveToProjectFolderCommand(host.db, projectGuid, guids,
                                                            target, outcome));
        if (!outcome->error.isEmpty())          // lost a race with another writer
            fail(QStringLiteral("assets.moveToFolder: %1").arg(outcome->error));
        return outcome->moved;
    }

    const projectfolders::Result result =
        projectfolders::moveTo(host.db, projectGuid, guids, target);
    if (!result.ok) {
        fail(QStringLiteral("assets.moveToFolder: %1").arg(result.error));
        return 0;
    }
    return result.moved;
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
    const bool onSurface = options.value(QStringLiteral("onSurface")).toBool();
    if (onSurface && !hasPosition) {
        fail("assets.addToScene: onSurface needs a position — it says what that position MEANS "
             "(the surface the model rests on), so on its own there is nothing to rest it on");
        return QString();
    }
    const auto placement = onSurface ? surfaceplacement::Placement::OnSurface
                                     : surfaceplacement::Placement::Pivot;
    host.services->selection->select(iris::SceneNodePtr());
    host.services->sceneEdit->addMaterialMesh(QString(), hasPosition,
                                              vecFromJs(options.value("position")), guid,
                                              record.name, placement);
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
    // The primitives come from the ONE table (src/data/primitives.h); rows
    // with no library guid (Ground) are not library builtins and are not
    // listed — `scene.addPrimitive("Ground")` is how that one is reached.
    for (const primitives::Def &def : primitives::all()) {
        if (!def.guid) continue;
        out.append(QVariantMap{ { "guid", QString::fromLatin1(def.guid) },
                                { "name", QString::fromLatin1(def.name) },
                                { "kind", QStringLiteral("primitive") } });
    }
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
    const bool wasMaterial =
        host.db->fetchAsset(guid).type == static_cast<int>(ModelTypes::Material);
    const auto outcome = assetdelete::remove(host.db, guid, keepShared, force);
    if (!outcome.ok) return fail(QStringLiteral("assets.remove: %1").arg(outcome.error));
    // A BUNDLE'S EXCLUSIVE BORN-INSIDE MEMBERS GO WITH IT (MATERIAL_BUNDLE_SPEC
    // §4), on a real delete only: an UNLIST keeps the material alive for the
    // projects that pin it. A picture the USER imported carries no origin
    // stamp and stays; one anything else uses stays; one any project pins
    // stays. `keepShared` is about the general dependency closure and cannot
    // answer this — it would take the user's own texture too.
    if (!outcome.unlisted && wasMaterial)
        materialmembers::reapExclusiveMembers(host.db, guid);
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

QVariantMap AssetsApi::refreshThumbnail(const QString &guid)
{
    QVariantMap out;
    out["ok"] = false;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    // MISUSE STILL THROWS (SCRIPTING_SPEC §1.6.1): a guid that names no asset
    // is a precondition failure, not an answer — unchanged by the new return
    // shape, which reports why a REAL asset's thumbnail could not be made.
    if (host.db->fetchAsset(guid).guid.isEmpty()) {
        fail(QStringLiteral("assets.refreshThumbnail: no asset with guid '%1'").arg(guid));
        return out;
    }

    // THE ONE ROUTINE (THUMBS-1). The switch that used to live here is
    // services/thumbnailrebuild.h, so the bulk repair below, the Assets page's
    // menu entry and this verb cannot drift apart — and the Avatar branch this
    // verb never had is in all three at once.
    const thumbrebuild::Outcome outcome =
        thumbrebuild::rebuildOne(host.db, host.project, guid, EngineHost::instance().engine());
    out["ok"] = outcome.ok;
    if (!outcome.ok) {
        out["reason"] = outcome.reason;
        // A REFUSAL, not a throw: the answer IS the return value, and the
        // reason travels in it (the caller can still read app.lastError()).
        refuse(QStringLiteral("assets.refreshThumbnail: %1").arg(outcome.reason));
    }
    return out;
}

QVariantMap AssetsApi::rebuildThumbnails(const QVariantMap &options)
{
    QVariantMap out;
    out["considered"] = 0;
    out["rebuilt"] = 0;
    out["skipped"] = 0;
    out["cancelled"] = false;
    out["failed"] = QVariantList();
    if (!host.db) { fail("assets: not available in this session"); return out; }

    for (auto it = options.constBegin(); it != options.constEnd(); ++it) {
        static const QStringList known{ QStringLiteral("missingOnly"), QStringLiteral("projectOnly"),
                                        QStringLiteral("limit") };
        if (!known.contains(it.key())) {
            fail(QStringLiteral("assets.rebuildThumbnails: unknown option '%1'").arg(it.key()));
            return out;
        }
    }

    thumbrebuild::SweepOptions sweep;
    if (options.contains("missingOnly")) sweep.missingOnly = options.value("missingOnly").toBool();
    if (options.contains("projectOnly")) sweep.projectOnly = options.value("projectOnly").toBool();
    if (options.contains("limit")) sweep.limit = options.value("limit").toInt();

    // THE YIELD (THUMBS-1 item 4): one engine render per asset, and a library
    // can hold hundreds. USER INPUT IS EXCLUDED — a click that re-entered the
    // sweep through a menu would be a second sweep over the same rows.
    //
    // AND IT IS STOPPABLE (fix round F1): a yield can deliver the window's
    // close, and a script can be stopped or time out while this one verb is
    // still rendering. ScriptEngine::stop and the app's shutdown both set the
    // flag thumbrebuild checks per row; `cancelled` comes back in the result
    // so a caller can tell "nothing left to do" from "we were stopped".
    const auto result = thumbrebuild::rebuildMissing(
        host.db, host.project, EngineHost::instance().engine(), sweep,
        [] { QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents); });

    QVariantList failed;
    for (const auto &f : result.failed)
        failed.append(QVariantMap{ { "guid", f.guid }, { "reason", f.reason } });
    out["considered"] = result.considered;
    out["rebuilt"] = result.rebuilt;
    out["skipped"] = result.skipped;
    out["cancelled"] = result.cancelled;
    out["failed"] = failed;
    return out;
}

// ---- the Assets PAGE (smoke S4/S5/S7) --------------------------------------
//
// Three verbs onto the page the owner was clicking: select the tile an import
// just made, read back what the preview is showing (so "the preview and the
// editor disagree about scale" is a test rather than a screenshot), and fly the
// preview camera. The page owns the widgets; these are the calls that drive
// them, and the page's own gestures call the same functions.

AssetView *AssetsApi::assetsPage(const QString &verb)
{
    AssetView *page = host.mainWindow ? host.mainWindow->assetsPage() : nullptr;
    if (!page) { fail(QStringLiteral("%1: there is no Assets page in this session").arg(verb)); return nullptr; }
    return page;
}

bool AssetsApi::select(const QString &guid)
{
    AssetView *page = assetsPage(QStringLiteral("assets.select"));
    if (!page) return false;
    if (guid.isEmpty()) return refuse("assets.select: no guid given");
    if (!page->selectAsset(guid))
        return refuse(QStringLiteral("assets.select: the Assets page shows no tile for '%1'").arg(guid));
    return true;
}

QString AssetsApi::selected()
{
    AssetView *page = assetsPage(QStringLiteral("assets.selected"));
    return page ? page->selectedAssetGuid() : QString();
}

QVariantMap AssetsApi::preview(const QVariantMap &options)
{
    QVariantMap out;
    AssetView *page = assetsPage(QStringLiteral("assets.preview"));
    if (!page) return out;
    const QString refusal = refuseUnknownKeys(QStringLiteral("assets.preview"), options, {});
    if (!refusal.isEmpty()) { fail(refusal); return out; }

    out["guid"] = page->selectedAssetGuid();
    auto *viewer = dynamic_cast<EngineAssetViewer *>(page->previewViewer());
    if (!viewer || !viewer->assetScene()) {
        refuse("assets.preview: this session has no engine asset preview");
        return out;
    }
    EngineAssetScene *scene = viewer->assetScene();
    auto subject = scene->subject();
    out["subject"] = subject ? subject->getName() : QString();

    const iris::AABB bounds = scene->subjectBounds();
    if (bounds.getMin().x() <= bounds.getMax().x()) {
        const iris::Vec3 mn = bounds.getMin(), mx = bounds.getMax();
        auto vec = [](const iris::Vec3 &v) {
            return QVariantMap{ { "x", v.x() }, { "y", v.y() }, { "z", v.z() } };
        };
        out["bounds"] = QVariantMap{ { "min", vec(mn) }, { "max", vec(mx) },
                                     { "size", vec(mx - mn) }, { "center", vec(bounds.getCenter()) } };
    }
    const iris::Vec3 pos = scene->cameraPosition(), pivot = scene->pivot();
    out["camera"] = QVariantMap{
        { "position", QVariantMap{ { "x", pos.x() }, { "y", pos.y() }, { "z", pos.z() } } },
        { "pivot", QVariantMap{ { "x", pivot.x() }, { "y", pivot.y() }, { "z", pivot.z() } } },
        { "distance", scene->distanceFromPivot() }
    };
    return out;
}

bool AssetsApi::fly(const QVariantMap &move)
{
    AssetView *page = assetsPage(QStringLiteral("assets.fly"));
    if (!page) return false;
    static const QStringList known = { QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z"),
                                       QStringLiteral("forward"), QStringLiteral("right"),
                                       QStringLiteral("up") };
    const QString refusal = refuseUnknownKeys(QStringLiteral("assets.fly"), move, known);
    if (!refusal.isEmpty()) return fail(refusal);

    auto *viewer = dynamic_cast<EngineAssetViewer *>(page->previewViewer());
    if (!viewer || !viewer->assetScene())
        return refuse("assets.fly: this session has no engine asset preview");
    EngineAssetScene *scene = viewer->assetScene();
    auto camera = scene->camera();
    if (!camera) return refuse("assets.fly: the preview has no camera");

    const double x = normalizeJs(move.value("x", 0)).toDouble();
    const double y = normalizeJs(move.value("y", 0)).toDouble();
    const double z = normalizeJs(move.value("z", 0)).toDouble();
    const double fwd = normalizeJs(move.value("forward", 0)).toDouble();
    const double rgt = normalizeJs(move.value("right", 0)).toDouble();
    const double up = normalizeJs(move.value("up", 0)).toDouble();

    // The camera-relative half is flystep's basis — the very directions the
    // arrow/WASD keys use, so a scripted fly and a held key are one motion.
    flystep::Keys keys;
    keys.forward = fwd > 0; keys.back = fwd < 0;
    keys.right = rgt > 0;   keys.left = rgt < 0;
    keys.up = up > 0;       keys.down = up < 0;
    iris::Vec3 delta = iris::Vec3(float(x), float(y), float(z));
    if (keys.any()) {
        const iris::Vec3 dir = flystep::direction(camera->getLocalRot(), keys);
        const float metres = float(std::max({ std::abs(fwd), std::abs(rgt), std::abs(up) }));
        delta += dir * metres;
    }
    if (delta.isNull()) return refuse("assets.fly: no movement given");
    scene->flyBy(delta);
    return true;
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
        // HOW MUCH OF THE TILE THE SUBJECT FILLS. A thumbnail can be textured
        // and still be wrong — the owner's ruined-city tile was a correct
        // render of a 50 m model framed for a box that also had to contain the
        // world origin, so it came out a smudge in the middle of 512x512
        // (smoke S6). Coverage is the fraction of pixels that differ from the
        // corner (the background the renderer cleared to), which is the number
        // that says so.
        const QRgb background = image.pixel(1, 1);
        const int bgR = qRed(background), bgG = qGreen(background), bgB = qBlue(background);
        qint64 lit = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QRgb p = image.pixel(x, y);
                if (std::abs(qRed(p) - bgR) > 8 || std::abs(qGreen(p) - bgG) > 8
                    || std::abs(qBlue(p) - bgB) > 8)
                    ++lit;
            }
        out["coverage"] = double(lit) / double(qMax(1, image.width() * image.height()));
    }
    return out;
}

QVariantMap AssetsApi::exportBundle(const QString &guid, const QString &path)
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    const auto written = assetshare::exportBundle(host.db, host.project, guid, path.trimmed());
    if (!written.ok()) {
        fail(QStringLiteral("assets.export: %1").arg(written.error));
        return out;
    }
    out["path"] = written.path;
    out["kind"] = written.kind;
    out["assets"] = written.assets;
    out["bytes"] = static_cast<qlonglong>(written.bytes);
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
    QVariantMap status = AssetStoreService::status(host.db);
    // THE NUMBER OF TIMES THIS PROCESS HAS WAITED FOR THE DEVICE, so that "no
    // durable write belongs on the thread that draws" (FSYNC-2) is a claim a
    // caller can CHECK rather than infer from row counts. Monotonic; only
    // differences mean anything.
    status.insert(QStringLiteral("deviceWaits"),
                  static_cast<qulonglong>(AssetCas::deviceWaits()));
    return status;
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
    QJsonObject record = service.importSettings(guid);
    if (record.isEmpty()) { fail(QStringLiteral("assets.importSettings: no import record for '%1'").arg(guid)); return out; }

    // The COMPLETE settings record, always. A row imported before the import
    // dialog has none, and "none" IS the defaults — that is what was applied to
    // it — so a caller never has to know this class's defaults to read one.
    record[QStringLiteral("settings")] = iris::ImportSettings::fromJson(
        record.value(QStringLiteral("settings")).toObject()).toJson();

    // …AND WHAT THE FILE ITSELF SAYS (§7), so a dialog and a test read one
    // truth instead of measuring their own: the declared unit and the size this
    // asset actually imported at.
    const AssetRecord row = host.db->fetchAsset(guid);
    // THE ROW'S OWN NAMES, so a caller that wants to TALK about this asset —
    // the import dialog's title, and the extension its pre-read of the stored
    // source needs, because the store names objects by content hash — reads
    // them from the verb instead of reaching past it into the database and the
    // CAS (API-first, SCRIPTING_SPEC §2.3).
    record[QStringLiteral("name")] = row.name;
    {
        QString sourceName;
        AssetCas::resolveSource(QSqlDatabase::database(), AssetStorePaths::root(), guid,
                                &sourceName);
        record[QStringLiteral("sourceName")] = sourceName.isEmpty() ? row.name : sourceName;
    }
    if (row.type == static_cast<int>(ModelTypes::Object)) {
        const QJsonObject meta = AssetMetadata::ensure(host.db, guid);
        QJsonObject defaults;
        defaults[QStringLiteral("declaredUnitScale")] =
            meta.value(QStringLiteral("unitScale")).toDouble(1.0);
        if (meta.contains(QStringLiteral("extent")))
            defaults[QStringLiteral("extent")] = meta.value(QStringLiteral("extent"));
        record[QStringLiteral("defaults")] = defaults;
    }
    return record.toVariantMap();
}

// REIMPORT (SPECS/IMPORT_DIALOG_SPEC.md §5) — the verb behind the Assets page's
// "Import settings…" button. The service does the work; this is the option
// surface and the OPEN-SCENE half, which is Studio's alone.
QVariantMap AssetsApi::reimport(const QString &guid, const QVariantMap &options)
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }

    // PARSED MERGED OVER THE STORED RECORD (the second read's F8), never
    // standalone: `{axes: {up: "+Z"}}` on its own is not a valid record —
    // the axis pair must be perpendicular and a partial axes object would
    // reset the other axis to the default — but merged over what the asset
    // already carries it is exactly the one-field edit the dialog makes.
    AssetImportService service(host.db, host.project);
    QJsonObject wanted = service.importSettings(guid).value(QStringLiteral("settings")).toObject();
    for (auto it = options.constBegin(); it != options.constEnd(); ++it)
        wanted.insert(it.key(), QJsonValue::fromVariant(scriptmod::normalizeJs(it.value())));
    QJsonObject settings;
    QString settingsError;
    if (!importSettingsFromOptions(QStringLiteral("assets.reimport"), wanted.toVariantMap(),
                                   &settings, &settingsError)) {
        fail(settingsError);
        return out;
    }

    const auto result = service.reimport(guid, settings);
    if (!result.ok()) {
        fail(QStringLiteral("assets.reimport: %1").arg(result.error));
        return out;
    }

    // THE OPEN SCENE (§5), through the SERVICE so the lane-2 dialog reuses the
    // same walk (SceneEditService::refreshAssetMeshes). The service's reimport
    // already dropped MeshBakeStore's caches, so this reads the new bake.
    const int swapped =
        (host.services && host.services->sceneEdit)
            ? host.services->sceneEdit->refreshAssetMeshes(result.meshGuid, result.sourcePath)
            : 0;

    out["guid"] = guid;
    out["settings"] = result.settings.toVariantMap();
    out["extent"] = result.metadata.value(QStringLiteral("extent")).toObject().toVariantMap();
    out["bakeOid"] = result.bakeOid;
    out["previousBakeOid"] = result.previousBakeOid;
    out["swappedNodes"] = swapped;
    if (host.isEngineReady()) refreshThumbnail(guid);
    return out;
}

QVariantMap AssetsApi::bakeAll(const QVariantMap &options)
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    const bool dryRun = options.value(QStringLiteral("dryRun"), true).toBool();

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    // BY (file, ROW): an asset's import settings are half the bake key, so two
    // rows over one store object with different settings are two bakes and a
    // by-content sweep would leave one of them parsing on every open forever
    // (the second read's F3).
    const QVector<MeshBakeStore::BakeTarget> needing =
        MeshBakeStore::modelBakesNeeded(conn, root);

    QVariantList errors;
    int baked = 0, failed = 0;
    if (!dryRun) {
        for (const MeshBakeStore::BakeTarget &target : needing) {
            const QString &path = target.path;
            QString error;
            if (MeshBakeStore::bakeSource(conn, root, path, &error, target.assetGuid)) {
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

QVariantMap AssetsApi::restampSeed()
{
    QVariantMap out;
    if (!host.db) { fail("assets: not available in this session"); return out; }
    // THE SAME CALL THE LAUNCH MAKES (services/materialpresetseeder.h runs it
    // beside its own pass), so the verb the test drives and the route a user
    // takes are one function over one shipped set.
    const presetrestamp::Report report =
        presetrestamp::restamp(host.db, MaterialPresetAssets::allGuids());
    if (!report.error.isEmpty()) {
        fail(QStringLiteral("assets.restampSeed: %1").arg(report.error));
        return out;
    }
    out.insert(QStringLiteral("stamped"), report.stamped);
    out.insert(QStringLiteral("presets"), report.presets);
    out.insert(QStringLiteral("skipped"), report.skipped);
    out.insert(QStringLiteral("scanned"), report.scanned);
    out.insert(QStringLiteral("ms"), static_cast<qint64>(report.ms));
    out.insert(QStringLiteral("ran"), report.ran);
    return out;
}
