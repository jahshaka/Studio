/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/assetsapi.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QStandardPaths>

#include "scripting/modules/moduleshared.h"
#include "export/exportcontentsource.h"
#include "export/rawexporter.h"
#include "services/assetservice.h"
#include "services/assetmigration.h"
#include "services/assetstore.h"
#include "services/assetstorepaths.h"
#include "data/constants.h"
#include "data/settingsmanager.h"
#include "services/assethelper.h"
#include "services/assetmetadata.h"
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
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"

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
    return -1;
}

QString storeFolderFor(const QString &guid)
{
    return AssetStorePaths::legacyFolder(guid);
}

} // namespace

QVector<VerbInfo> AssetsApi::verbs() const
{
    return {
        { "list", "assets.list({scope: 'store'|'project', type}) -> [{guid, name, type, drawer}]",
          "Store assets (default) or the open project's assets, optionally filtered by type name. A type-filtered project listing sweeps every folder (materials registered under Presets/ included); unfiltered it lists the root folder. drawer is the containing drawer's id (0 = Uncategorized).",
          Needs::Document },
        { "metadata", "assets.metadata(guid) -> {guid, name, type, imported, kind, format, fileSize, ...}",
          "Rich per-type metadata for a store asset. Models: vertices, triangles, meshes, materials, textures; images: width, height; audio (wav): duration (ms), sampleRate, channels, bitsPerSample; video: duration (ms), width, height, frameRate, videoCodec; every kind: format + fileSize. Computed at import since the metadata feature landed; for older rows the first call computes it from the store files and persists it (lazy backfill).",
          Needs::Document },
        { "import", "assets.import(path) -> guid",
          "Imports a mesh file (obj, fbx, dae, blend, glb, gltf) into the global asset store. NOT undoable.",
          Needs::Document },
        { "importFile", "assets.importFile(path, drawerId?) -> guid",
          "Imports any library-supported file (models, images, audio, video) into the asset store, optionally filed in a drawer. Images/audio/video are headless-safe (video decodes through Qt Multimedia's ffmpeg backend, no display needed). NOT undoable.",
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
          "Copies a store asset (files + DB rows + dependencies, fresh guids) into the open project; returns the project-side guid. NOT undoable.",
          Needs::Document },
        { "addToScene", "assets.addToScene(guid, {position}) -> nodeId",
          "Instantiates a project object asset into the scene (undoable, like a drag from the asset browser).",
          Needs::Document },
        { "builtins", "assets.builtins() -> [{guid, name, kind}]",
          "The reserved built-ins: primitives, materials and shaders with their reserved guids. Guids collide across kinds — always pair guid with kind.",
          Needs::Document },
        { "remove", "assets.remove(guid, {keepShared: true}) -> bool",
          "Deletes a store asset: its rows, its store folder, and (keepShared false) its dependency assets too. PERMANENT — no undo.",
          Needs::Document },
        { "refreshThumbnail", "assets.refreshThumbnail(guid) -> bool",
          "Rebuilds an asset's thumbnail synchronously and writes it to the database. Objects and materials render on the engine (engine required); images re-thumbnail from the source file, videos re-grab a first-second frame, and audio/file rows reset to their type icon (document-only).",
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
        { "migrateStore", "assets.migrateStore({dbPath, root}) -> report",
          "Migrates a legacy per-guid store into the content-addressed store: hashes every library file (view_filter 2 and 3), hardlinks/copies into objects/, writes files/asset_files rows + rebuild sidecars + store.json. The legacy tree is RETAINED; a missing folder is zero files; idempotent (rerun = zero new objects). Refuses while another Jahshaka instance holds the library. dbPath/root default to the live library — pass both to rehearse against a copy.",
          Needs::Document },
        { "verify", "assets.verify({dbPath, root}) -> report",
          "Re-hashes every catalogued object against its oid: reports corrupt (bit-rot) and missing objects with counts and bytes. Defaults to the live library.",
          Needs::Document },
        { "rebuildCatalog", "assets.rebuildCatalog(dbPath, {root}) -> report",
          "Reconstructs catalog rows (assets + files + asset_files) from the store's sidecar/*.json into the given database — the disaster-recovery path. dbPath is REQUIRED (rebuilding into the live catalog is not implied); existing guids are left untouched; thumbnails are regenerable, not recovered.",
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

    QVector<AssetRecord> records;
    if (scope == "store") {
        records = host.db->fetchAssetsForAssetView();
    } else if (scope == "project") {
        if (!requireProject()) return out;
        if (typeFilter >= 0) {
            // Folder-independent: a type-filtered project listing must see
            // assets registered in subfolders too (a preset apply files its
            // material asset under Presets/, which a root-children sweep
            // never returned).
            for (const auto &record : host.db->fetchFilteredAssets(host.project->getProjectGuid(), typeFilter)) {
                out.append(QVariantMap{ { "guid", record.guid },
                                        { "name", record.name },
                                        { "type", typeName(typeFilter) },
                                        { "drawer", record.collection } });
            }
            return out;
        }
        records = host.db->fetchChildAssets(host.project->getProjectGuid(), host.project->getProjectGuid(), -1, true);
    } else {
        fail("assets.list: scope must be 'store' or 'project'");
        return out;
    }

    for (const auto &record : records) {
        if (typeFilter >= 0 && record.type != typeFilter) continue;
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
    if (record.dateCreated.isValid())
        out["imported"] = record.dateCreated.toString(Qt::ISODate);
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
    if (host.isEngineReady()) refreshThumbnail(result.objectGuid);
    return result.objectGuid;
}

QString AssetsApi::importFile(const QString &path, int drawerId)
{
    if (!host.services || !host.services->assets) { fail("assets: not available in this session"); return QString(); }
    const auto result = host.services->assets->importFile(path, drawerId);
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

    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        fail(QStringLiteral("assets.addToProject: no asset with guid '%1'").arg(guid));
        return QString();
    }
    const int aType = record.type;

    // The widget's post-Toast body (assetview.cpp addAssetItemToProject):
    // copy the store folder's files into the project (deduplicating names),
    // register them in AssetManager, then Database::copyAsset clones the rows.
    const QDir sourceDir(storeFolderFor(guid));
    const auto files = sourceDir.entryInfoList(QDir::Files);
    const QString projectFolder = host.project->getProjectFolder();
    const QString placeHolderGuid = GUIDManager::generateGUID();
    QVector<QPair<QString, QString>> copied;   // original name -> new name

    for (const auto &file : files) {
        QString newName = file.fileName();
        int increment = 1;
        while (QFileInfo::exists(QDir(projectFolder).filePath(newName)))
            newName = QStringLiteral("%1 %2.%3").arg(file.baseName()).arg(increment++).arg(file.suffix());
        const QString destination = QDir(projectFolder).filePath(newName);
        QFile::copy(file.absoluteFilePath(), destination);
        copied.append({ file.fileName(), newName });

        const ModelTypes fileType = AssetHelper::getAssetTypeFromExtension(file.suffix());
        if (fileType == ModelTypes::File) {
            auto asset = new AssetFile;
            asset->fileName = newName;
            asset->assetGuid = placeHolderGuid;
            asset->path = destination;
            AssetManager::addAsset(asset);
        } else if (fileType == ModelTypes::Texture) {
            auto asset = new AssetTexture;
            asset->fileName = newName;
            asset->assetGuid = placeHolderGuid;
            asset->path = destination;
            AssetManager::addAsset(asset);
        } else if (fileType == ModelTypes::Mesh) {
            // No SceneSource: the loader now owns a local importer when none
            // is passed (the old `new` here leaked the whole parsed scene).
            auto node = iris::MeshNode::loadAsSceneFragment(
                destination, [](iris::MeshPtr, iris::MeshMaterialData &data) {
                    auto mat = iris::CustomMaterial::create();
                    mat->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
                    mat->setValue("diffuseColor", data.diffuseColor);
                    mat->setValue("specularColor", data.specularColor);
                    mat->setValue("ambientColor", data.ambientColor);
                    mat->setValue("emissionColor", data.emissionColor);
                    mat->setValue("shininess", data.shininess);
                    return mat;
                });
            if (node) {
                auto asset = new AssetNodeObject;
                asset->fileName = newName;
                asset->assetGuid = placeHolderGuid;
                asset->path = destination;
                asset->setValue(QVariant::fromValue(node));
                AssetManager::addAsset(asset);
            }
        }
    }
    QMap<QString, QString> newNames;
    for (const auto &pair : copied) newNames.insert(pair.first, pair.second);

    ModelTypes jafType = ModelTypes::File;
    switch (static_cast<ModelTypes>(aType)) {
    case ModelTypes::Material:
    case ModelTypes::Object:
    case ModelTypes::Texture:
    case ModelTypes::Shader:
    case ModelTypes::Sky:
    case ModelTypes::ParticleSystem:
        jafType = static_cast<ModelTypes>(aType);
        break;
    default: break;
    }

    QVector<AssetRecord> newRecords;
    const QString newGuid = host.db->copyAsset(jafType, guid, newNames, newRecords,
                                               host.project->getProjectGuid(),
                                               AssetViewFilter::Editor,
                                               host.project->getProjectGuid());

    // Post-copy registrations, per type (the widget's tail).
    for (auto &asset : AssetManager::getAssets()) {
        if (asset->assetGuid != placeHolderGuid) continue;
        if (asset->type == ModelTypes::Object) {
            asset->assetGuid = newGuid;
            auto node = asset->getValue().value<iris::SceneNodePtr>();
            if (node) {
                auto definition = QJsonDocument::fromJson(host.db->fetchAssetData(newGuid)).object();
                AssetHelper::updateNodeMaterial(node, definition);
            }
        } else {
            // Files/textures: adopt the guid of the matching copied row.
            for (const auto &rec : newRecords)
                if (rec.name == asset->fileName) asset->assetGuid = rec.guid;
            if (asset->assetGuid == placeHolderGuid) asset->assetGuid = newGuid;
        }
    }

    if (jafType == ModelTypes::Material) {
        auto matObject = QJsonDocument::fromJson(host.db->fetchAssetData(newGuid)).object();
        MaterialReader reader;
        reader.setProject(host.project);
        auto material = reader.parseMaterialTyped(matObject, host.db);
        auto asset = new AssetMaterial;
        asset->assetGuid = newGuid;
        asset->setValue(QVariant::fromValue(material));
        AssetManager::addAsset(asset);
    } else if (jafType == ModelTypes::Shader) {
        for (const auto &rec : newRecords) {
            if (rec.type != static_cast<int>(ModelTypes::Shader)) continue;
            auto assetShader = new AssetShader;
            assetShader->assetGuid = rec.guid;
            assetShader->fileName = QFileInfo(rec.name).baseName();
            assetShader->setValue(QVariant::fromValue(QJsonDocument::fromJson(rec.asset).object()));
            AssetManager::addAsset(assetShader);
        }
    } else if (jafType == ModelTypes::ParticleSystem) {
        auto particleObject = QJsonDocument::fromJson(host.db->fetchAssetData(newGuid)).object();
        auto asset = new AssetParticleSystem;
        asset->assetGuid = newGuid;
        asset->setValue(QVariant::fromValue(particleObject));
        AssetManager::addAsset(asset);
    }

    return newGuid;
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

QVariantList AssetsApi::builtins()
{
    QVariantList out;
    const auto append = [&out](const QMap<QString, QString> &map, const char *kind) {
        for (auto it = map.constBegin(); it != map.constEnd(); ++it)
            out.append(QVariantMap{ { "guid", it.key() }, { "name", it.value() }, { "kind", kind } });
    };
    append(Constants::Reserved::DefaultPrimitives, "primitive");
    append(Constants::Reserved::DefaultMaterials, "material");
    append(Constants::Reserved::BuiltinShaders, "shader");
    return out;
}

bool AssetsApi::remove(const QString &guid, const QVariantMap &options)
{
    if (!host.db) return fail("assets: not available in this session");
    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty())
        return fail(QStringLiteral("assets.remove: no asset with guid '%1'").arg(guid));

    const bool keepShared = options.value("keepShared", true).toBool();
    if (keepShared) {
        // Conservative: only the asset row and its dependency links go;
        // dependee assets (possibly shared) stay.
        host.db->deleteAsset(guid);
        host.db->deleteDependency(guid);
        for (const auto &dep : host.db->fetchAssetGUIDAndDependencies(guid, false))
            host.db->deleteDependency(guid, dep);
    } else {
        host.db->deleteAssetAndDependencies(guid);
    }

    QDir storeDir(storeFolderFor(guid));
    if (storeDir.exists()) storeDir.removeRecursively();
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
        auto thumb = ThumbnailManager::createThumbnail(
            IrisUtils::join(storeFolderFor(guid), record.name), 256, 256);
        if (!thumb || !thumb->thumb || thumb->thumb->isNull())
            return fail("assets.refreshThumbnail: could not read the image");
        return host.db->updateAssetThumbnail(
            guid, AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(*thumb->thumb)));
    }
    if (record.type == static_cast<int>(ModelTypes::Music)) {
        return host.db->updateAssetThumbnail(
            guid, AssetHelper::makeBlobFromPixmap(
                      QPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-file-music.png"))));
    }
    if (record.type == static_cast<int>(ModelTypes::Video)) {
        // First-second frame re-grab; VideoUtils falls back to the film icon
        // when decode fails, so this always writes something sensible.
        const QPixmap thumb =
            VideoUtils::thumbnailFor(IrisUtils::join(storeFolderFor(guid), record.name));
        return host.db->updateAssetThumbnail(guid, AssetHelper::makeBlobFromPixmap(thumb));
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
        const QString storeDir = storeFolderFor(guid);
        if (QDir(storeDir).exists()) reader.setBaseDirectory(storeDir);
        else if (host.project) reader.setBaseDirectory(host.project->getProjectFolder());
        auto blob = QJsonDocument::fromJson(host.db->fetchAssetData(guid)).object();
        iris::SceneNodePtr node = reader.readSceneNode(blob);
        if (!node) return fail("assets.refreshThumbnail: could not rebuild the object");
        image = renderer.renderNode(node, QSize(512, 512));
    } else if (record.type == static_cast<int>(ModelTypes::Material)) {
        auto matObject = QJsonDocument::fromJson(host.db->fetchAssetData(guid)).object();
        MaterialReader reader;
        reader.setProject(host.project);
        image = renderer.renderMaterial(reader.parseMaterialTyped(matObject, host.db), QSize(512, 512));
    } else {
        renderer.release();
        return fail("assets.refreshThumbnail: only object and material assets are supported");
    }
    renderer.release();

    if (image.isNull()) return fail("assets.refreshThumbnail: the render produced no image");
    return host.db->updateAssetThumbnail(guid, AssetHelper::makeBlobFromPixmap(QPixmap::fromImage(image)));
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
        if (rec.guid.isEmpty()) continue;   // dangling dependency edge — skip, not fatal
        RawExporter::AssetInfo info;
        info.guid = rec.guid;
        info.name = rec.name;
        info.typeId = rec.type;
        info.type = typeName(rec.type);
        info.dependencies = host.db->fetchAssetGUIDAndDependencies(rec.guid, false);
        infos.append(info);
    }

    // Explicit store root: exporters never derive storage paths themselves.
    // storeRootPath() is the canonical helper Lane A folds into AssetStorePaths.
    LegacyStoreContentSource source(AssetMetadata::storeRootPath(), hash);
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
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(Constants::JAH_DATABASE);
}
} // namespace

QVariantMap AssetsApi::migrateStore(const QVariantMap &options)
{
    const QString dbPath = options.value("dbPath", liveDbPath()).toString();
    const QString root = options.value("root", AssetStorePaths::root()).toString();
    const auto report = AssetMigration::migrateStore(dbPath, root);
    if (!report.ok) fail(QStringLiteral("assets.migrateStore: %1").arg(report.error));
    return report.toMap();
}

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
