/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/lightbindings.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QSqlDatabase>

#include "data/database/database.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetmetadata.h"
#include "services/assetstorepaths.h"
#include "services/projectassets.h"
#include "irisgl/document/scenegraph/lightnode.h"

namespace {

/// Guid → bytes, pin-first (a project pin, then the library source). The exact
/// route SceneReader::resolveAssetPath takes for a project load: the renderer
/// must open the same bytes the reader would.
QString resolvePath(const QString &guid, Project *project)
{
    if (guid.isEmpty()) return QString();
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    if (project && !project->getProjectGuid().isEmpty()) {
        const QString pinned =
            AssetCas::resolvePinned(conn, root, project->getProjectGuid(), guid);
        if (!pinned.isEmpty()) return pinned;
    }
    return AssetCas::resolveSource(conn, root, guid);
}

}  // namespace

float LightBindings::normalisationFor(const QString &guid, Database *db)
{
    if (guid.isEmpty() || !db) return 1.0f;
    const QJsonObject meta = AssetMetadata::ensure(db, guid);
    const double factor = meta.value(QStringLiteral("normalisationFactor")).toDouble(1.0);
    return factor > 1e-6 ? float(factor) : 1.0f;
}

bool LightBindings::bindProfile(const iris::LightNodePtr &light, const QString &guid,
                                Database *db, Project *project, QString *errorOut)
{
    if (!light) {
        if (errorOut) *errorOut = QStringLiteral("no light node");
        return false;
    }
    if (guid.isEmpty()) {
        light->iesProfileGuid.clear();
        light->iesProfilePath.clear();
        light->iesNormalisation = 1.0f;
        return true;
    }
    if (!db) {
        if (errorOut) *errorOut = QStringLiteral("no library");
        return false;
    }
    const auto record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("no asset with guid '%1'").arg(guid);
        return false;
    }
    if (static_cast<ModelTypes>(record.type) != ModelTypes::LightProfile) {
        if (errorOut)
            *errorOut = QStringLiteral("'%1' is not a light profile (.ies) asset")
                            .arg(record.name);
        return false;
    }

    // A DEPENDENCY add: the scene refers to this asset, the user did not add it
    // to the project. Never a companion material, never a bin tile of its own.
    if (project && !project->getProjectGuid().isEmpty())
        ProjectAssets::addToProject(guid, db, project, ProjectAssets::AddKind::Binding);

    const QString path = resolvePath(guid, project);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        if (errorOut)
            *errorOut = QStringLiteral("the bytes of '%1' are not in the asset store")
                            .arg(record.name);
        return false;
    }

    light->iesProfileGuid = guid;
    light->iesProfilePath = path;
    light->iesNormalisation = normalisationFor(guid, db);
    return true;
}

bool LightBindings::bindTexture(const iris::LightNodePtr &light, const QString &guid,
                                Database *db, Project *project, QString *errorOut)
{
    if (!light) {
        if (errorOut) *errorOut = QStringLiteral("no light node");
        return false;
    }
    if (guid.isEmpty()) {
        light->lightTextureGuid.clear();
        light->lightTexturePath.clear();
        return true;
    }
    if (!db) {
        if (errorOut) *errorOut = QStringLiteral("no library");
        return false;
    }
    const auto record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("no asset with guid '%1'").arg(guid);
        return false;
    }
    if (static_cast<ModelTypes>(record.type) != ModelTypes::Texture) {
        if (errorOut)
            *errorOut = QStringLiteral("'%1' is not an image asset").arg(record.name);
        return false;
    }

    if (project && !project->getProjectGuid().isEmpty())
        ProjectAssets::addToProject(guid, db, project, ProjectAssets::AddKind::Binding);

    const QString path = resolvePath(guid, project);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        if (errorOut)
            *errorOut = QStringLiteral("the bytes of '%1' are not in the asset store")
                            .arg(record.name);
        return false;
    }

    light->lightTextureGuid = guid;
    light->lightTexturePath = path;
    return true;
}

void LightBindings::resolve(const iris::LightNodePtr &light, Database *db, Project *project)
{
    if (!light) return;
    if (!light->iesProfileGuid.isEmpty()) {
        light->iesProfilePath = resolvePath(light->iesProfileGuid, project);
        light->iesNormalisation = normalisationFor(light->iesProfileGuid, db);
    } else {
        light->iesProfilePath.clear();
        light->iesNormalisation = 1.0f;
    }
    light->lightTexturePath = light->lightTextureGuid.isEmpty()
                                  ? QString()
                                  : resolvePath(light->lightTextureGuid, project);
}
