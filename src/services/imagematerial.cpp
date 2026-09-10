/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/imagematerial.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "io/scenewriter.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "irisgl/document/materials/pbrmaterial.h"

namespace ImageMaterial
{

iris::PbrMaterialPtr fromTexture(const QString &textureGuid, Database *db,
                                 Project *project, QString *resolvedPathOut,
                                 bool *hasAlphaOut)
{
    if (resolvedPathOut) resolvedPathOut->clear();
    if (hasAlphaOut) *hasAlphaOut = false;
    if (!db || textureGuid.isEmpty()) return iris::PbrMaterialPtr();

    // Pin-first byte resolution; resolvePinned itself falls back to the
    // library source, so store context (no open project) works too. Never a
    // projectFolder join (the flat folder died with the pin world).
    QSqlDatabase conn = QSqlDatabase::database();
    const QString projectGuid =
        project ? project->getProjectGuid() : QString();
    const QString path = AssetCas::resolvePinned(conn, AssetStorePaths::root(),
                                                 projectGuid, textureGuid);
    if (path.isEmpty()) return iris::PbrMaterialPtr();

    // One decode serves both the alpha probe and setValue's texture load.
    const QImage image(path);
    if (image.isNull()) return iris::PbrMaterialPtr();
    const bool hasAlpha = image.hasAlphaChannel();

    auto material = iris::PbrMaterial::create();
    // setValue (not the raw fields) so the editor-facing Property objects
    // stay in step with what the shader reads.
    material->setValue(QStringLiteral("baseColorMap"), path);
    material->setValue(QStringLiteral("roughness"), 1.0f);
    material->setValue(QStringLiteral("metallic"), 0.0f);
    if (hasAlpha) {
        // Owner call (§8.2): BLEND, so the plane carries the image's true
        // alpha; the sorting caveat is accepted, cutout stays manual.
        material->setValue(QStringLiteral("alphaMode"), 2);
    }

    if (resolvedPathOut) *resolvedPathOut = path;
    if (hasAlphaOut) *hasAlphaOut = hasAlpha;
    return material;
}

QString createMaterialAsset(const QString &textureGuid, Database *db,
                            Project *project, QString *errorOut)
{
    if (errorOut) errorOut->clear();
    auto failWith = [errorOut](const QString &message) {
        if (errorOut) *errorOut = message;
        return QString();
    };
    if (!db) return failWith(QStringLiteral("no database"));

    const auto record = db->fetchAsset(textureGuid);
    if (record.guid.isEmpty()
        || static_cast<ModelTypes>(record.type) != ModelTypes::Texture)
        return failWith(QStringLiteral("'%1' is not a texture asset").arg(textureGuid));

    QString resolvedPath;
    auto material = fromTexture(textureGuid, db, project, &resolvedPath);
    if (!material)
        return failWith(QStringLiteral("'%1' resolves to no readable image").arg(record.name));

    // Serialize like MaterialImporter: writeSceneNodeMaterial, then the
    // texture reference becomes the ASSET GUID (readers resolve pin-first
    // through the CAS — no path ever reaches the stored definition).
    QJsonObject blob;
    SceneWriter::writeSceneNodeMaterial(blob, material, /*relative=*/false);
    QJsonObject values = blob.value(QStringLiteral("values")).toObject();
    values[QStringLiteral("baseColorMap")] = textureGuid;
    blob[QStringLiteral("values")] = values;

    const QString matName = QFileInfo(record.name).completeBaseName();
    blob[QStringLiteral("name")] = matName;

    // THE STAMP (code review 2026-09-10). What makes this row "the image's own
    // material" is that WE minted it for that image — not its shape. The shape
    // test that stood in for it ("a Material whose only dependee is this
    // texture") matched a material the USER built on the same image just as
    // well, and would have taken theirs out of a project with the image. The
    // stamp is written once, here, at the only place that mints one.
    blob[QStringLiteral("companionOf")] = textureGuid;

    // Thumbnail straight from the image — headless-safe, no engine render.
    QByteArray thumbnail;
    {
        const QImage thumb =
            QImage(resolvedPath).scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (!thumb.isNull()) {
            QBuffer buffer(&thumbnail);
            buffer.open(QIODevice::WriteOnly);
            thumb.save(&buffer, "PNG");
        }
    }

    const QString materialGuid = GUIDManager::generateGUID();
    db->createAssetEntry(materialGuid, matName,
                         static_cast<int>(ModelTypes::Material),
                         QString(),           // library row: no parent folder
                         QString(),           // library row: no project guid
                         QString(), QString(), thumbnail,
                         QByteArray(), QByteArray(),
                         QJsonDocument(blob).toJson(),
                         AssetViewFilter::AssetsView);
    db->createDependency(static_cast<int>(ModelTypes::Material),
                         static_cast<int>(ModelTypes::Texture),
                         materialGuid, textureGuid, QString());
    return materialGuid;
}

bool hasCompanionMaterial(const QString &textureGuid)
{
    QSqlQuery query(QSqlDatabase::database());
    query.prepare("SELECT 1 FROM dependencies WHERE dependee = ? AND depender_type = ? LIMIT 1");
    query.addBindValue(textureGuid);
    query.addBindValue(static_cast<int>(ModelTypes::Material));
    return query.exec() && query.next();
}

QStringList companionMaterials(const QString &textureGuid)
{
    QStringList companions;
    if (textureGuid.isEmpty()) return companions;
    QSqlDatabase conn = QSqlDatabase::database();
    if (!conn.isOpen()) return companions;

    // THE STAMP IS THE TEST: `companionOf` names the texture this material was
    // minted for (createMaterialAsset above). A material the user authored on
    // the same image carries no stamp and is never one of these, whatever its
    // shape. Rows minted before the stamp existed answer no — nothing is owed
    // to old data (CRUD law), and the cost of answering no is a leftover pin
    // the user removes by hand.
    //
    // The candidate set still comes from the dependency edges, so this costs
    // one query plus one blob read per material that names this texture.
    QSqlQuery query(conn);
    query.prepare("SELECT depender FROM dependencies WHERE dependee = ? AND depender_type = ?");
    query.addBindValue(textureGuid);
    query.addBindValue(static_cast<int>(ModelTypes::Material));
    if (!query.exec()) return companions;

    QStringList candidates;
    while (query.next()) candidates << query.value(0).toString();

    for (const QString &candidate : candidates) {
        QSqlQuery blob(conn);
        blob.prepare("SELECT asset FROM assets WHERE guid = ?");
        blob.addBindValue(candidate);
        if (!blob.exec() || !blob.next()) continue;
        const QJsonObject definition =
            QJsonDocument::fromJson(blob.value(0).toByteArray()).object();
        if (definition.value(QStringLiteral("companionOf")).toString() != textureGuid) continue;

        // Still UNEDITED: a companion the user has since given a second map is
        // theirs now, and a project keeps it. (The stamp says where the row
        // came from; this says it has not become something else.)
        QSqlQuery deps(conn);
        deps.prepare("SELECT dependee FROM dependencies WHERE depender = ?");
        deps.addBindValue(candidate);
        if (!deps.exec()) continue;
        QStringList dependees;
        while (deps.next()) dependees << deps.value(0).toString();
        dependees.removeDuplicates();
        if (dependees.size() == 1 && dependees.first() == textureGuid)
            companions << candidate;
    }
    return companions;
}

} // namespace ImageMaterial
