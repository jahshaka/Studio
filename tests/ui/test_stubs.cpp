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
#include "irisgl/document/materials/pbrmaterial.h"

// CALL COUNTERS — ui.db_handles asserts that the library bookkeeping a panel
// skips without a library really happens once the library arrives. Every other
// suite ignores them.
int gStubCreateDependencyCalls = 0;
int gStubRemoveDependenciesCalls = 0;
int gStubUpdateAssetAssetCalls = 0;

bool Database::createDependency(const int &, const int &, const QString &,
                                const QString &, const QString &)
{
    ++gStubCreateDependencyCalls;
    return false;
}

bool Database::deleteDependency(const QString &, const QString &)
{
    return false;
}

bool Database::removeDependenciesByType(const QString &, const ModelTypes &)
{
    ++gStubRemoveDependenciesCalls;
    return false;
}

bool Database::updateAssetAsset(const QString &, const QByteArray &)
{
    ++gStubUpdateAssetAssetCalls;
    return false;
}

AssetRecord Database::fetchAsset(const QString &)
{
    return AssetRecord();
}

MaterialReader::MaterialReader(TextureSource texSrc)
    : textureSource(texSrc)
{
}

iris::PbrMaterialPtr MaterialReader::createMaterialFromShaderGuid(QString, Database *,
                                                                  const QJsonObject &)
{
    return iris::PbrMaterialPtr();
}

/// The one guid this stub answers for (ui.db_handles): the material blade's
/// combo slot only reaches its library bookkeeping when the pick RESOLVES, so
/// a suite about that slot needs one material that does.
extern const char *const kStubResolvableMaterialGuid;
extern const char *const kStubResolvableMaterialGuid = "stub-resolvable-material";

iris::MaterialPtr MaterialReader::parseShaderAsPbr(const QString &guid, Database *)
{
    if (guid == QLatin1String(kStubResolvableMaterialGuid))
        return iris::PbrMaterial::create().staticCast<iris::Material>();
    return iris::MaterialPtr();
}

void SceneWriter::writeSceneNode(QJsonObject &, iris::SceneNodePtr, bool)
{
}

#include "services/sceneeditservice.h"

void SceneEditService::notifyTransformChanged()
{
}

// The material panel's "Reset to Default Floor" (services/materialdefaults.h)
// calls this; the reset itself is proven by scripting.e2e.default_floor.
bool SceneEditService::resetMaterial(iris::SceneNodePtr)
{
    return false;
}
