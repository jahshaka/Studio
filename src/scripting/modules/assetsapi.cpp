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
#include "services/assetservice.h"
#include "data/constants.h"
#include "services/assethelper.h"
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
    switch (static_cast<ModelTypes>(type)) {
    case ModelTypes::Material: return "material";
    case ModelTypes::Texture: return "texture";
    case ModelTypes::Video: return "video";
    case ModelTypes::Sky: return "sky";
    case ModelTypes::Object: return "object";
    case ModelTypes::Mesh: return "mesh";
    case ModelTypes::SoundEffect: return "soundeffect";
    case ModelTypes::Music: return "music";
    case ModelTypes::Shader: return "shader";
    case ModelTypes::Variant: return "variant";
    case ModelTypes::File: return "file";
    case ModelTypes::ParticleSystem: return "particles";
    default: return "undefined";
    }
}

int typeFromName(const QString &name)
{
    const QString n = name.trimmed().toLower();
    if (n == "material") return static_cast<int>(ModelTypes::Material);
    if (n == "texture") return static_cast<int>(ModelTypes::Texture);
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
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + Constants::ASSET_FOLDER + "/" + guid;
}

} // namespace

QVector<VerbInfo> AssetsApi::verbs() const
{
    return {
        { "list", "assets.list({scope: 'store'|'project', type}) -> [{guid, name, type, drawer}]",
          "Store assets (default) or the open project's assets, optionally filtered by type name. drawer is the containing drawer's id (0 = Uncategorized).",
          Needs::Document },
        { "import", "assets.import(path) -> guid",
          "Imports a mesh file (obj, fbx, dae, blend, glb, gltf) into the global asset store. NOT undoable.",
          Needs::Document },
        { "importFile", "assets.importFile(path, drawerId?) -> guid",
          "Imports any library-supported file (models, images, audio) into the asset store, optionally filed in a drawer. Images/audio are headless-safe. NOT undoable.",
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
          "Re-renders an object or material asset's thumbnail synchronously and writes it to the database.",
          Needs::Engine },
        { "dependencies", "assets.dependencies(guid) -> [guid]",
          "The asset plus all its dependencies, recursively.",
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
            auto ssource = new iris::SceneSource();   // required: the loader dereferences it
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
                }, ssource);
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
        auto material = reader.parseMaterial(matObject, host.db);
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
    if (!requireEngine()) return false;
    auto engine = EngineHost::instance().engine();
    if (!engine) return fail("assets.refreshThumbnail: the engine is not running");

    const auto record = host.db->fetchAsset(guid);
    if (record.guid.isEmpty())
        return fail(QStringLiteral("assets.refreshThumbnail: no asset with guid '%1'").arg(guid));

    QImage image;
    EngineThumbnailRenderer renderer(engine);
    if (record.type == static_cast<int>(ModelTypes::Object)) {
        // Prefer the session-registered node (import registers one); fall back
        // to rebuilding from the blob with the store folder as the base dir.
        iris::SceneNodePtr node;
        for (auto &asset : AssetManager::getAssets()) {
            if (asset->assetGuid == guid && asset->type == ModelTypes::Object) {
                node = asset->getValue().value<iris::SceneNodePtr>();
                if (node) break;
            }
        }
        if (!node) {
            SceneReader reader;
            reader.setDatabaseHandle(host.db);
            reader.setProject(host.project);
            const QString storeDir = storeFolderFor(guid);
            reader.setBaseDirectory(QDir(storeDir).exists() && host.project
                                        ? storeDir : host.project->getProjectFolder());
            auto blob = QJsonDocument::fromJson(host.db->fetchAssetData(guid)).object();
            node = reader.readSceneNode(blob);
        }
        if (!node) return fail("assets.refreshThumbnail: could not rebuild the object");
        image = renderer.renderNode(node, QSize(512, 512));
    } else if (record.type == static_cast<int>(ModelTypes::Material)) {
        auto matObject = QJsonDocument::fromJson(host.db->fetchAssetData(guid)).object();
        MaterialReader reader;
        reader.setProject(host.project);
        image = renderer.renderMaterial(reader.parseMaterial(matObject, host.db), QSize(512, 512));
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
