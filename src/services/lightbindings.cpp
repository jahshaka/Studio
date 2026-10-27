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
#include "irisgl/document/assets/texture2d.h"
#include <QImage>

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
        light->markChanged(iris::NodeChange::Params);
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
    // THE CHANGE MARK (SPECS/DIRTY_SET_MIRROR_SPEC.md): these are reflected
    // fields written by hand rather than through setPropertyValue, and the
    // mirror only looks at what the document says changed.
    light->markChanged(iris::NodeChange::Params);
    return true;
}

QSharedPointer<iris::Texture2D> LightBindings::loadSkyCube(const QJsonObject &faces,
                                                           const std::function<QString(const QString &)> &resolve)
{
    // createCubeMap's argument order is front, back, TOP, BOTTOM, left, right (skyassets' note: the wrong order rotates
    // the dome silently).
    static const char *roles[6] = { "front", "back", "top", "bottom", "left", "right" };
    QString sides[6];
    for (int i = 0; i < 6; ++i) {
        const QString guid = faces.value(QLatin1String(roles[i])).toString();
        sides[i] = guid.isEmpty() ? QString() : resolve(guid);
        if (sides[i].isEmpty() || !QFileInfo::exists(sides[i])) return QSharedPointer<iris::Texture2D>();
    }
    QImage info(sides[0]);
    if (info.isNull()) return QSharedPointer<iris::Texture2D>();
    return iris::Texture2D::createCubeMap(sides[0], sides[1], sides[2], sides[3], sides[4], sides[5], &info);
}

bool LightBindings::bindSkyCube(const iris::LightNodePtr &light, const QJsonObject &faces, Database *db,
                                Project *project, QString *errorOut)
{
    if (!light || light->lightType != iris::LightType::Sky) {
        if (errorOut) *errorOut = QStringLiteral("not a Sky Light");
        return false;
    }
    if (faces.isEmpty()) {   // the scene's sky (the default)
        light->skySource = iris::SkyLightSource::Sky;
        light->skyCubeFaces = QJsonObject();
        light->skyCubeTexture.reset();
        light->markChanged(iris::NodeChange::Params);
        return true;
    }
    if (!db) {
        if (errorOut) *errorOut = QStringLiteral("no library");
        return false;
    }
    static const char *roles[6] = { "front", "back", "top", "bottom", "left", "right" };
    QJsonObject kept;
    for (const char *role : roles) {
        const QString guid = faces.value(QLatin1String(role)).toString();
        const bool texture = !guid.isEmpty() && [&]() {
            const auto record = db->fetchAsset(guid);
            return !record.guid.isEmpty() && static_cast<ModelTypes>(record.type) == ModelTypes::Texture;
        }();
        if (!texture) {
            if (errorOut)
                *errorOut = QStringLiteral("the dome's '%1' face is not a texture asset (all six faces: front, back, "
                                           "top, bottom, left, right)").arg(QLatin1String(role));
            return false;
        }
        kept.insert(QLatin1String(role), guid);
    }
    auto cube = loadSkyCube(kept, [project](const QString &guid) { return resolvePath(guid, project); });
    if (!cube) {
        if (errorOut) *errorOut = QStringLiteral("a dome face's bytes are not in the asset store, or do not decode");
        return false;
    }
    // A DEPENDENCY add per face, as the profile's: the scene refers to them, the user did not add them.
    if (project && !project->getProjectGuid().isEmpty())
        for (auto it = kept.begin(); it != kept.end(); ++it)
            ProjectAssets::addToProject(it.value().toString(), db, project, ProjectAssets::AddKind::Binding);
    light->skySource = iris::SkyLightSource::Cubemap;
    light->skyCubeFaces = kept;
    light->skyCubeTexture = cube;
    light->markChanged(iris::NodeChange::Params);
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
    light->markChanged(iris::NodeChange::Params);
}
