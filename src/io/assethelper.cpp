/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "assethelper.h"

#include <QPixmap>
#include <QBuffer>

#include "irisgl/src/core/property.h"
#include "irisgl/src/materials/custommaterial.h"
#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/scenegraph/meshnode.h"

#include "io/scenewriter.h"
#include "io/assetmanager.h"

// Thanks to Qt not allowing updating its json values and instead returning temp objects
// This class updates a meshnode with the values in a material definition
// We handle special cases such as textures that need to be matched with guids
// and colors that need to be converted from hex, the rest can be implicitly set
void AssetHelper::updateNodeMaterial(iris::SceneNodePtr &node, const QJsonObject definition)
{
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto materialDefinition = definition.value("material").toObject();
        auto nodeMaterial = node.staticCast<iris::MeshNode>()->getMaterial().staticCast<iris::CustomMaterial>();

        QFileInfo shaderFile;
        QMapIterator<QString, QString> it(Constants::Reserved::BuiltinShaders);
        while (it.hasNext()) {
            it.next();
            if (it.key() == materialDefinition["guid"].toString()) {
                shaderFile = QFileInfo(IrisUtils::getAbsoluteAssetPath(it.value()));
                break;
            }
        }

        if (shaderFile.exists()) {
            nodeMaterial->generate(shaderFile.absoluteFilePath());
        }
        else {
            for (auto asset : AssetManager::getAssets()) {
                if (asset->type == ModelTypes::Shader) {
                    if (asset->assetGuid == materialDefinition["guid"].toString()) {
                        auto def = asset->getValue().toJsonObject();
                        auto vertexShader = def["vertex_shader"].toString();
                        auto fragmentShader = def["fragment_shader"].toString();
                        for (auto asset : AssetManager::getAssets()) {
                            if (asset->type == ModelTypes::File) {
                                if (vertexShader == asset->assetGuid) vertexShader = asset->path;
                                if (fragmentShader == asset->assetGuid) fragmentShader = asset->path;
                            }
                        }
                        def["vertex_shader"] = vertexShader;
                        def["fragment_shader"] = fragmentShader;

						nodeMaterial->setMaterialDefinition(def);
                        nodeMaterial->generate(def);
                    }
                }
            }
        }

        for (const iris::Property* property : nodeMaterial->properties) {
            if (property->type == iris::PropertyType::Texture) {
				QString textureValue = materialDefinition.value(property->name).toString();
                if (!textureValue.isEmpty() && !QFileInfo(textureValue).suffix().isEmpty()) {
                    nodeMaterial->setValue(property->name, materialDefinition.value(property->name).toString());
                }
            }
            else if (property->type == iris::PropertyType::Color) {
                nodeMaterial->setValue(
                    property->name,
                    QVariant::fromValue(materialDefinition.value(property->name).toVariant().value<QColor>())
                );
            }
            else {
                nodeMaterial->setValue(property->name, QVariant::fromValue(materialDefinition.value(property->name)));
            }
        }
    }

    QJsonArray children = definition["children"].toArray();
    // These will always be in sync since the definition is derived from the mesh
    if (node->hasChildren() && (node->getSceneNodeType() == iris::SceneNodeType::Mesh)) {
        for (int i = 0; i < node->children.count(); i++) {
            updateNodeMaterial(node->children[i], children[i].toObject());
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
    QStringList topLevelAssets;
    QStringList assetAndDependencies;

    for (const auto &asset : db->fetchAssetGUIDAndDependencies(guid)) {
        topLevelAssets.append(asset);
        assetAndDependencies.append(asset);
    }

    // Dependency level is always max 2
    for (const auto &asset : topLevelAssets) {
        for (const auto &guid : db->fetchAssetGUIDAndDependencies(asset)) {
            assetAndDependencies.append(guid);
        }
    }

    assetAndDependencies.removeDuplicates();

    return assetAndDependencies;
}

ModelTypes AssetHelper::getAssetTypeFromExtension(const QString &fileSuffix)
{
    if (Constants::IMAGE_EXTS.contains(fileSuffix)) {
        return ModelTypes::Texture;
    }
    else if (Constants::MODEL_EXTS.contains(fileSuffix)) {
        return ModelTypes::Mesh;
    }
    else if (fileSuffix == Constants::SHADER_EXT) {
        return ModelTypes::Shader;
    }
    else if (fileSuffix == Constants::MATERIAL_EXT) {
        return ModelTypes::Material;
    }
    else if (Constants::WHITELIST.contains(fileSuffix)) {
        return ModelTypes::File;
    }

    // Generally if we don't explicitly specify an extension, don't import or add it to the database (iKlsR)
    return ModelTypes::Undefined;
}

iris::SceneNodePtr AssetHelper::extractTexturesAndMaterialFromMesh(
    const QString &filePath,
    QStringList &textureList)
{
    auto ssource = new iris::SceneSource();
    // load mesh as scene
    auto node = iris::MeshNode::loadAsSceneFragment(filePath, [&](iris::MeshPtr mesh, iris::MeshMaterialData& data)
    {
        auto mat = iris::CustomMaterial::create();

        if (mesh->hasSkeleton())
            mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/DefaultAnimated.shader"));
        else
            mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

        mat->setValue("diffuseColor", data.diffuseColor);
        mat->setValue("specularColor", data.specularColor);
        mat->setValue("ambientColor", data.ambientColor);
        mat->setValue("emissionColor", data.emissionColor);
        mat->setValue("shininess", data.shininess);
        mat->setValue("useAlpha", true);

        if (QFile(data.diffuseTexture).exists() && QFileInfo(data.diffuseTexture).isFile())
            mat->setValue("diffuseTexture", data.diffuseTexture);

        if (QFile(data.specularTexture).exists() && QFileInfo(data.specularTexture).isFile())
            mat->setValue("specularTexture", data.specularTexture);

        if (QFile(data.normalTexture).exists() && QFileInfo(data.normalTexture).isFile())
            mat->setValue("normalTexture", data.normalTexture);

        return mat;
    }, ssource);

    const aiScene *scene = ssource->importer.GetScene();

    QStringList texturesToCopy;

    for (int i = 0; i < scene->mNumMeshes; i++) {
        auto mesh = scene->mMeshes[i];
        auto material = scene->mMaterials[mesh->mMaterialIndex];

        aiString textureName;

        if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
            material->GetTexture(aiTextureType_DIFFUSE, 0, &textureName);
            texturesToCopy.append(textureName.C_Str());
        }

        if (material->GetTextureCount(aiTextureType_SPECULAR) > 0) {
            material->GetTexture(aiTextureType_SPECULAR, 0, &textureName);
            texturesToCopy.append(textureName.C_Str());
        }

        if (material->GetTextureCount(aiTextureType_NORMALS) > 0) {
            material->GetTexture(aiTextureType_NORMALS, 0, &textureName);
            texturesToCopy.append(textureName.C_Str());
        }

        if (material->GetTextureCount(aiTextureType_HEIGHT) > 0) {
            material->GetTexture(aiTextureType_HEIGHT, 0, &textureName);
            texturesToCopy.append(textureName.C_Str());
        }
    }

    textureList = texturesToCopy;
    // SceneWriter::writeSceneNode(QJsonObject(), node, false);

    return node;
}
