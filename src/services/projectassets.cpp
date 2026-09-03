/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/projectassets.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "io/assetmanager.h"
#include "io/materialreader.h"
#include "services/assetcas.h"
#include "services/assethelper.h"
#include "services/assetstorepaths.h"
#include "services/imagematerial.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"

namespace {

QString sourceOidOf(QSqlDatabase conn, const QString &guid)
{
    QSqlQuery query(conn);
    query.prepare("SELECT oid FROM asset_files WHERE asset_guid = ? "
                  "ORDER BY CASE role WHEN 'source' THEN 0 ELSE 1 END, name");
    query.addBindValue(guid);
    if (query.exec() && query.next()) return query.value(0).toString();
    return QString();
}

bool sessionHas(const QString &guid)
{
    for (auto *asset : AssetManager::getAssets())
        if (asset && asset->assetGuid == guid) return true;
    return false;
}

} // namespace

ProjectAssets::Result ProjectAssets::addToProject(const QString &guid, Database *db,
                                                  Project *project, AddKind kind)
{
    Result result;
    if (!db || !project || project->getProjectGuid().isEmpty()) {
        result.error = QStringLiteral("no open project");
        return result;
    }
    const auto record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        result.error = QStringLiteral("no asset with guid '%1'").arg(guid);
        return result;
    }

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QString projectGuid = project->getProjectGuid();

    // The membership = the asset plus its dependency closure, each pinned at
    // its CURRENT source content. No files move; no rows clone.
    const QStringList members = AssetHelper::fetchAssetAndAllDependencies(guid, db);
    for (const QString &member : members) {
        AssetCas::writePin(conn, projectGuid, member, sourceOidOf(conn, member));
        result.pinnedGuids.append(member);
    }

    // Session registrations — the ORIGINAL guids, bytes CAS-resolved through
    // the fresh pins (identical to what the readers will resolve).
    for (const QString &member : members)
        registerSessionAsset(member, db, project);

    // Owner call (IMAGE_PLANE_SPEC §8.1, 2026-08-31): an image added to a
    // project ALSO gets its companion material asset — created in the
    // library, then pinned in through this same function so it lands in the
    // bin, session-registered and droppable. BOUNDARY: only the DIRECTLY
    // added asset auto-creates — dependency textures riding an object's
    // closure never do (an object with 30 textures must not explode into 30
    // materials), a texture pinned as a BINDING (a light's mask or IES
    // profile, and later a decal's maps — AddKind::Binding) never does
    // either, and re-adding the same image is a no-op (a Material depending
    // on the texture already exists). The recursive addToProject cannot loop:
    // the companion is a Material, and Materials never auto-create.
    if (kind == AddKind::Direct
        && static_cast<ModelTypes>(record.type) == ModelTypes::Texture
        && !ImageMaterial::hasCompanionMaterial(guid)) {
        const QString materialGuid = ImageMaterial::createMaterialAsset(guid, db, project);
        if (!materialGuid.isEmpty()) {
            const Result companion = addToProject(materialGuid, db, project, AddKind::Direct);
            result.pinnedGuids.append(companion.pinnedGuids);
            result.pinnedGuids.removeDuplicates();
        }
    }

    result.guid = guid;
    return result;
}

bool ProjectAssets::registerSessionAsset(const QString &guid, Database *db,
                                         Project *project)
{
    if (!db || !project || project->getProjectGuid().isEmpty()) return false;
    if (sessionHas(guid)) return true;
    const auto memberRecord = db->fetchAsset(guid);
    if (memberRecord.guid.isEmpty()) return false;

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QString projectGuid = project->getProjectGuid();
    const QString member = guid;
    const QString path = AssetCas::resolvePinned(conn, root, projectGuid, member);

    switch (static_cast<ModelTypes>(memberRecord.type)) {
        case ModelTypes::Object: {
            if (path.isEmpty()) break;
            auto node = iris::MeshNode::loadAsSceneFragment(
                path, [](iris::MeshPtr, iris::MeshMaterialData &data) {
                    auto mat = iris::CustomMaterial::create();
                    mat->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
                    mat->setValue("diffuseColor", data.diffuseColor);
                    mat->setValue("specularColor", data.specularColor);
                    mat->setValue("ambientColor", data.ambientColor);
                    mat->setValue("emissionColor", data.emissionColor);
                    mat->setValue("shininess", data.shininess);
                    return iris::MaterialPtr(mat);
                });
            if (!node) break;
            const auto definition = QJsonDocument::fromJson(db->fetchAssetData(member)).object();
            AssetHelper::updateNodeMaterial(node, definition, db);
            auto *asset = new AssetNodeObject;
            asset->assetGuid = member;
            asset->fileName = memberRecord.name;
            asset->path = path;
            asset->setValue(QVariant::fromValue(node));
            AssetManager::addAsset(asset);
            break;
        }
        case ModelTypes::Texture: {
            auto *asset = new AssetTexture;
            asset->assetGuid = member;
            asset->fileName = memberRecord.name;
            asset->path = path;
            AssetManager::addAsset(asset);
            break;
        }
        case ModelTypes::Music: {
            auto *asset = new AssetMusic;
            asset->assetGuid = member;
            asset->fileName = memberRecord.name;
            asset->path = path;
            AssetManager::addAsset(asset);
            break;
        }
        case ModelTypes::File: {
            auto *asset = new AssetFile;
            asset->assetGuid = member;
            asset->fileName = memberRecord.name;
            asset->path = path;
            AssetManager::addAsset(asset);
            break;
        }
        case ModelTypes::Shader: {
            auto *asset = new AssetShader;
            asset->assetGuid = member;
            asset->fileName = QFileInfo(memberRecord.name).baseName();
            asset->setValue(QVariant::fromValue(
                QJsonDocument::fromJson(db->fetchAssetData(member)).object()));
            AssetManager::addAsset(asset);
            break;
        }
        case ModelTypes::Material: {
            const auto matObject = QJsonDocument::fromJson(db->fetchAssetData(member)).object();
            MaterialReader reader;
            reader.setProject(project);
            auto material = reader.parseMaterialTyped(matObject, db);
            auto *asset = new AssetMaterial;
            asset->assetGuid = member;
            asset->fileName = memberRecord.name;
            asset->setValue(QVariant::fromValue(material));
            AssetManager::addAsset(asset);
            break;
        }
        case ModelTypes::ParticleSystem: {
            auto *asset = new AssetParticleSystem;
            asset->assetGuid = member;
            asset->fileName = QFileInfo(memberRecord.name).baseName();
            asset->setValue(QVariant::fromValue(
                QJsonDocument::fromJson(db->fetchAssetData(member)).object()));
            AssetManager::addAsset(asset);
            break;
        }
        default:
            return false;   // no session shape for this type (Mesh rows etc.)
        }
    return true;
}

bool ProjectAssets::updatePinToLatest(const QString &guid, Database *db, Project *project)
{
    if (!db || !project || project->getProjectGuid().isEmpty()) return false;
    QSqlDatabase conn = QSqlDatabase::database();
    return AssetCas::writePin(conn, project->getProjectGuid(), guid,
                              sourceOidOf(conn, guid));
}

QString ProjectAssets::copyOnWrite(const QString &guid, const QString &newContentPath,
                                   Database *db, Project *project, QString *errorOut)
{
    if (!db || !project || project->getProjectGuid().isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("no open project");
        return QString();
    }
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();

    // The edited bytes become a new object recorded under the asset. The
    // asset_files PK (guid, role, name) keeps the LIBRARY mapping on its
    // original oid — only this project's pin moves (I3: content immutable,
    // catalog moves pointers).
    QString oid;
    if (!AssetCas::ingestFile(conn, root, newContentPath, guid,
                              QStringLiteral("source"),
                              QFileInfo(newContentPath).fileName(), &oid, errorOut))
        return QString();
    if (!AssetCas::writePin(conn, project->getProjectGuid(), guid, oid)) {
        if (errorOut) *errorOut = QStringLiteral("could not move the project pin");
        return QString();
    }
    return oid;
}
