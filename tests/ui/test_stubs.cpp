/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// Link stubs for ui.material_panel: the panel TUs reference the Studio
// database, the material reader and the scene writer (all only reachable
// through UI actions the test never takes - the shader selector combo and the
// texture-dependency bookkeeping, which is db-guarded). Stubbing them keeps
// the suite from linking Sql/assimp and half the io/ layer.

#include <QJsonObject>
#include <QString>

#include "data/database/database.h"
#include "io/materialreader.h"
#include "io/scenewriter.h"

bool Database::createDependency(const int &, const int &, const QString &,
                                const QString &, const QString &)
{
    return false;
}

bool Database::deleteDependency(const QString &, const QString &)
{
    return false;
}

bool Database::removeDependenciesByType(const QString &, const ModelTypes &)
{
    return false;
}

bool Database::updateAssetAsset(const QString &, const QByteArray &)
{
    return false;
}

AssetRecord Database::fetchAsset(const QString &)
{
    return AssetRecord();
}

QString Database::fetchAssetGUIDByName(const QString &name, const QString &)
{
    // ONE resolvable name, so a panel suite can drive an asset row end to end
    // (the sky section's equirect pick, and anything else that turns a picked
    // file into a library guid). Everything else is unknown, as before — the
    // rows that must cope with "not in the library" still get that answer.
    return name == QLatin1String("stub-asset.png") ? QStringLiteral("stub-guid") : QString();
}

MaterialReader::MaterialReader(TextureSource texSrc, QString globalSourceFolder)
    : textureSource(texSrc), globalSourceFolder(globalSourceFolder)
{
}

iris::PbrMaterialPtr MaterialReader::createMaterialFromShaderGuid(QString, Database *,
                                                                  const QJsonObject &)
{
    return iris::PbrMaterialPtr();
}

iris::MaterialPtr MaterialReader::parseShaderAsPbr(const QString &, Database *)
{
    return iris::MaterialPtr();
}

void SceneWriter::writeSceneNode(QJsonObject &, iris::SceneNodePtr, bool)
{
}

#include "services/sceneeditservice.h"

void SceneEditService::notifyTransformChanged()
{
}
