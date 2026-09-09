/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assethelper.h"

#include <QSet>

#include <QPixmap>
#include <QBuffer>
#include <QAtomicInt>

#include "irisgl/core/properties/property.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"

#include "io/scenewriter.h"
#include "io/assetmanager.h"
#include "io/builtinmaterials.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/assetmetadata.h"
#include <QSqlDatabase>

// Thanks to Qt not allowing updating its json values and instead returning temp objects
// This class updates a meshnode with the values in a material definition
// We handle special cases such as textures that need to be matched with guids
// and colors that need to be converted from hex, the rest can be implicitly set
void AssetHelper::updateNodeMaterial(iris::SceneNodePtr &node, QJsonObject definition,
                                     Database *db)
{
    // Texture values in stored definitions are member asset guids; every
    // branch below must resolve them to store files before setValue() hands
    // them to Texture2D::load (same CAS resolution as
    // MaterialReader::resolveTextureGuid). Old blobs that stored plain paths
    // still work: an unresolvable value falls back to itself.
    Q_UNUSED(db);
    const auto resolveTexture = [&](const QString &stored) -> QString {
        if (stored.isEmpty()) return stored;
        QSqlDatabase conn = QSqlDatabase::database();
        const QString path = AssetCas::resolveSource(conn, AssetStorePaths::root(), stored);
        return path.isEmpty() ? stored : path;
    };

    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto materialDefinition = definition.value("material").toObject();

        // ONE PATH SINCE HLMS_ADOPTION P4b, because there is one material class.
        //
        // A "pbr"-tagged blob rebuilds a PbrMaterial from its own rows; a
        // LEGACY blob (a builtin shader guid plus Default-shader uniform names)
        // has those names renamed to their PBR equivalents first, and then
        // drives the same rows. What used to sit here was a second copy of the
        // reader: it looked the builtin `.shader` file up, ran generate() to
        // get a property list, and walked THAT — so a blob the shader lookup
        // missed produced a material with no properties at all and every value
        // was silently dropped.
        const bool isPbrBlob =
            materialDefinition.value("materialType").toString() == QStringLiteral("pbr");

        // V1 definitions carry values at the top level; V2 (everything the
        // one-pipeline importer writes) nests them under "values". Read both.
        QJsonObject values = materialDefinition.value("values").toObject();
        for (auto it = materialDefinition.constBegin(); it != materialDefinition.constEnd(); ++it)
            if (!values.contains(it.key())) values.insert(it.key(), it.value());
        if (!isPbrBlob) values = BuiltinMaterials::normaliseLegacyDefinition(values);

        auto pbr = iris::PbrMaterial::create();
        for (const iris::Property* property : pbr->properties) {
            if (!values.contains(property->name)) continue;
            if (property->type == iris::PropertyType::Color)
                pbr->setValue(property->name,
                              QVariant::fromValue(values.value(property->name).toVariant().value<QColor>()));
            else if (property->type == iris::PropertyType::Texture)
                pbr->setValue(property->name,
                              resolveTexture(values.value(property->name).toString()));
            else
                pbr->setValue(property->name, values.value(property->name).toVariant());
        }
        // The Flat builtin is the one legacy guid whose SHADING MODEL differs
        // (HLMS_ADOPTION P4a/D-P4b), and a values-only walk cannot know that.
        if (!isPbrBlob) {
            const QString guid = materialDefinition.value("guid").toString().isEmpty()
                                     ? materialDefinition.value("shaderGuid").toString()
                                     : materialDefinition.value("guid").toString();
            if (BuiltinMaterials::isBuiltin(guid)) {
                auto builtin = BuiltinMaterials::fromBuiltin(guid, values, resolveTexture);
                pbr->setValue(QStringLiteral("shadingModel"), builtin->shadingModel);
            }
        }
        node.staticCast<iris::MeshNode>()->setMaterial(pbr);
    }

    QJsonArray children = definition["children"].toArray();
    // These will always be in sync since the definition is derived from the mesh
    if (!children.isEmpty()) {
        // ONE list, not one per iteration AND one per index: `node->children()`
        // materialises a QList and refcounts every child, and this loop called
        // it twice per step — an O(n^2) rebuild of the sibling list to walk it
        // once.
        QList<iris::SceneNodePtr> kids = node->children();
        for (int i = 0; i < kids.count() && i < children.size(); ++i) {
            if (!children[i].toObject().isEmpty())
                updateNodeMaterial(kids[i], children[i].toObject(), db);
        }
    }
}

QByteArray AssetHelper::makeBlobFromPixmap(const QPixmap &thumbnail)
{
    QByteArray thumbnailBytes;
    QBuffer buffer(&thumbnailBytes);
    buffer.open(QIODevice::WriteOnly);
    thumbnail.save(&buffer, "PNG");
    return thumbnailBytes;
}

QStringList AssetHelper::fetchAssetAndAllDependencies(const QString &guid, Database *db)
{
    // RECURSIVE, with a visited set (AVATAR_ASSET_SPEC §4 D11). This used to
    // be a hand-unrolled two levels with the comment "Dependency level is
    // always max 2" — true when the deepest shape in the tree was
    // Object -> {Mesh, Texture}. An Avatar row is Avatar -> Object ->
    // {Mesh, Texture}, which sits EXACTLY at that ceiling, so the next edge
    // anyone adds would silently drop files from pins and from archives
    // (both walk this list) with nothing failing. A closure that is a closure
    // cannot be one edge away from being wrong.
    //
    // The visited set is not defensive coding: a dependency table is a graph
    // the user can cycle through (asset A's material references a texture
    // whose material references A), and an unbounded walk of one would hang
    // the import instead of refusing it.
    QStringList assetAndDependencies;
    QSet<QString> seen;
    QStringList frontier;

    const auto push = [&](const QString &candidate) {
        if (candidate.isEmpty() || seen.contains(candidate)) return;
        seen.insert(candidate);
        assetAndDependencies.append(candidate);
        frontier.append(candidate);
    };

    for (const auto &asset : db->fetchAssetGUIDAndDependencies(guid)) push(asset);

    while (!frontier.isEmpty()) {
        const QString asset = frontier.takeFirst();
        for (const auto &dependency : db->fetchAssetGUIDAndDependencies(asset)) push(dependency);
    }

    return assetAndDependencies;
}

// Allows us to get all the child guids from the node being exported as dependencies
QStringList AssetHelper::getChildGuids(const iris::SceneNodePtr &node)
{
    // Allows us to get all the child guids from the node being exported as dependencies
    std::function<void(const iris::SceneNodePtr&, QStringList&)> getChildGuids =
        [&](const iris::SceneNodePtr &node, QStringList &items) -> void
    {
        if (!node->getGUID().isEmpty() && !items.contains(node->getGUID())) items.append(node->getGUID());
        if (node->hasChildren()) {
            for (const auto &child : node->children()) {
                getChildGuids(child, items);
            }
        }
    };

    QStringList assetGuids;
    getChildGuids(node, assetGuids);

    return assetGuids;
}

ModelTypes AssetHelper::getAssetTypeFromExtension(const QString &fileSuffix)
{
    if (Constants::IMAGE_EXTS.contains(fileSuffix)) {
        return ModelTypes::Texture;
    }
    else if (Constants::MODEL_EXTS.contains(fileSuffix)) {
        return ModelTypes::Mesh;
    }
	else if (Constants::AUDIO_EXTS.contains(fileSuffix)) {
		return ModelTypes::Music;
	}
	else if (Constants::VIDEO_EXTS.contains(fileSuffix)) {
		return ModelTypes::Video;
	}
    else if (fileSuffix == Constants::SHADER_EXT) {
        return ModelTypes::Shader;
    }
    else if (Constants::MATERIAL_EXTS.contains(fileSuffix)) {
        return ModelTypes::Material;
    }
    else if (Constants::WHITELIST.contains(fileSuffix)) {
        return ModelTypes::File;
    }

    // Generally if we don't explicitly specify an extension, don't import or add it to the database (iKlsR)
    return ModelTypes::Undefined;
}

// One full assimp parse per call — the import suites assert the completion
// tail never adds a second one on top of the pipeline's convert.
static QAtomicInt sMeshParseCount;

int AssetHelper::meshParseCount()
{
    return sMeshParseCount.loadRelaxed();
}

iris::SceneNodePtr AssetHelper::extractTexturesAndMaterialFromMesh(
    const QString &filePath,
    QStringList &textureList,
    QStringList &texturesFullPath,
    bool& hasEmbeddedTexture,
    QJsonObject *modelStats,
    const QString &extractDir,
    iris::SceneSource *keepScene)
{
    sMeshParseCount.fetchAndAddRelaxed(1);
    // Owns the assimp importer (and with it the aiScene) for the duration of
    // this function only — it used to be `new` with no delete, leaking the
    // entire parsed scene per import. A caller that needs the aiScene AFTER
    // the call (the mesh bake) passes its own SceneSource instead.
    QScopedPointer<iris::SceneSource> localSource;
    iris::SceneSource *ssource = keepScene;
    if (!ssource) {
        localSource.reset(new iris::SceneSource());
        ssource = localSource.data();
    }
    // load mesh as scene
    auto node = iris::MeshNode::loadAsSceneFragment(filePath, [&](iris::MeshPtr mesh, iris::MeshMaterialData& data)
    {
        const auto isFile = [](const QString &p) {
            return !p.isEmpty() && QFileInfo::exists(p) && QFileInfo(p).isFile();
        };

        // glTF/GLB (and any source with PBR data) imports as a REAL
        // PbrMaterial — factors and maps straight from the file. The old path
        // forced everything into the legacy Default.shader CustomMaterial,
        // whose shininess round-trip faked roughness to ~0.1 (GLB importer fix
        // phase 0; serialization handles "pbr" natively).
        //
        // ONE CONVERSION, NOT TWO (2026-09-08). This used to be a SECOND copy
        // of BuiltinMaterials::fromMeshData, and it had drifted: it set
        // emissiveColor but never emissiveIntensity, so every model imported
        // through the pipeline — this is the pipeline's own path — got a white
        // emissive colour multiplied by intensity 0, and no import could carry
        // emission at all. Duplicated policy is how that happens; the copy is
        // gone.
        if (data.hasPbr) {
            if (isFile(data.baseColorTexture))
                hasEmbeddedTexture = hasEmbeddedTexture || data.hasEmbeddedDiffTexture;
            return iris::MaterialPtr(BuiltinMaterials::fromMeshData(data));
        }

        // No pbrMetallicRoughness in the source: the legacy Blinn fields go
        // through the shared conversion (io/builtinmaterials.h) instead of the
        // Default.shader CustomMaterial this used to build. An embedded diffuse
        // texture still neutralises the colour — assimp reports a tint AND the
        // baked texture, and multiplying them darkened every embedded-texture
        // import.
        if (data.hasEmbeddedDiffTexture && !data.diffuseTexture.isEmpty()) {
            hasEmbeddedTexture = true;
            data.diffuseColor = QColor(255, 255, 255);
        }
        return iris::MaterialPtr(BuiltinMaterials::fromMeshData(data));
    }, ssource, nullptr, extractDir);

    const aiScene *scene = ssource->importer.GetScene();

    // Import-time metadata (ASSET_DRAWERS_SPEC addendum): count from the
    // scene assimp just loaded — no second parse of the file, ever.
    if (modelStats && scene)
        *modelStats = AssetMetadata::forModelScene(scene, filePath);

    QStringList texturesToCopy;



    std::function<void(iris::SceneNodePtr&)> getUsedTexture = [&](iris::SceneNodePtr &node) -> void {
        if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            auto n = node.staticCast<iris::MeshNode>();
            // Base MaterialPtr, no downcast: `properties` lives on iris::Material
            // and the material may now be a PbrMaterial (imported GLB) as well
            // as the legacy CustomMaterial.
            auto mat = n->getMaterial();
            if (mat) for (auto prop : mat->properties) {
                if (prop->type != iris::PropertyType::Texture) {
                    continue;
                }

                QString path = prop->getValue().toString();
                if (path.isEmpty()) {
                    continue;
                }

                QFileInfo fileinfo(path);
                if (QFile(path).exists() && fileinfo.isFile()) {
                    texturesFullPath.append(path);
                    texturesToCopy.append(fileinfo.fileName());
                }
            }
        }

        if (node->hasChildren()) {
            for (auto &child : node->children()) {
                getUsedTexture(child);
            }
        }
    };

    // assimp may have failed (e.g. a Draco-compressed glb — Draco is not
    // compiled in): return the null node and let the caller surface the error.
    if (node) getUsedTexture(node);

    textureList = texturesToCopy;
    // SceneWriter::writeSceneNode(QJsonObject(), node, false);

    return node;
}
