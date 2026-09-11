/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/defaultfloor.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/core/logger.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "services/shippedassets.h"

namespace defaultfloor {

QString meshPath() { return QStringLiteral(":/models/ground.obj"); }

QString shippedTilePath()
{
    return IrisUtils::getAbsoluteAssetPath("app/content/textures/tile.png");
}

iris::PbrMaterialPtr createMaterial(Database *db, Project *project, QString *tileGuid,
                                    bool *tileNewlyPinned)
{
    // THE GROUND'S TILE IS A LIBRARY TEXTURE (plan item 15c, audit D35). In a
    // real project it goes through the one import pipeline the first time any
    // project needs it (identified by its bytes, so every later project reuses
    // the same row) and is PINNED here: the material holds the pinned store
    // object, the writer saves its guid through the CAS and both readers
    // resolve it pin-first — the same round trip as any texture a user
    // imports, and a project export carries it.
    //
    // The STARTUP PLACEHOLDER project (Project::createNew: no guid, no folder)
    // never saves, so it renders the shipped file directly and writes nothing
    // to the library.
    QString tilePath = shippedTilePath();
    if (tileGuid) tileGuid->clear();
    if (tileNewlyPinned) *tileNewlyPinned = false;
    if (db && project && !project->getProjectGuid().isEmpty()) {
        const ShippedAssets::Pinned tile =
            ShippedAssets::pinTexture(tilePath, QStringLiteral("Tile.png"), db, project);
        if (tile.ok() && !tile.guid.isEmpty()) {
            tilePath = tile.path;
            if (tileGuid) *tileGuid = tile.guid;
            if (tileNewlyPinned) *tileNewlyPinned = tile.newlyPinned;
        } else {
            // The floor still renders (the shipped file); what is lost is the
            // guid on save, so say why instead of saving a scene that reopens
            // untextured with nothing in the log.
            irisLog("defaultfloor: the ground tile could not be pinned into the project - "
                    + tile.error);
        }
    }

    // A PbrMaterial (HLMS_ADOPTION P4b). The roughness is what the legacy
    // Default shader's shininess 0 already meant through the mirror's remap.
    auto material = iris::PbrMaterial::create();
    material->setValue("baseColorMap", tilePath);
    material->setValue("textureScale", kTextureScale);
    material->setValue("roughness", kRoughness);
    material->setValue("metallic", kMetallic);
    return material;
}

iris::MeshNodePtr createNode(Database *db, Project *project)
{
    auto node = iris::MeshNode::create();
    node->setMesh(meshPath());
    node->setLocalPos(iris::Vec3(0, 1e-4, 0)); // prevent z-fighting with the default plane reset (iKlsR)
    node->setName("Ground");
    node->setPickable(false);
    node->setFaceCullingMode(iris::FaceCullingMode::None);
    node->setShadowCastingEnabled(false);
    node->isBuiltIn = true;
    node->defaultFloor = true;
    const QString nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);

    {
        // A static physics plane.
        iris::PhysicsProperty physicsProperties;
        physicsProperties.objectMass = .0f;
        physicsProperties.isStatic = true;
        physicsProperties.objectCollisionMargin = .1f;
        physicsProperties.objectRestitution = .01f;
        physicsProperties.type = iris::PhysicsType::Static;
        physicsProperties.shape = iris::PhysicsCollisionShape::Plane;

        node->isPhysicsBody = true;
        node->physicsProperty = physicsProperties;
    }

    const bool realProject = db && project && !project->getProjectGuid().isEmpty();
    if (realProject) {
        // The node's own Object row, carrying the built-in marker: the export
        // walkers hang the node's edges off it, and the tray knows it is a
        // node rather than a library asset (services/assettray.h, rule 4).
        QJsonObject props;
        props.insert("type", "builtin");
        db->createAssetEntry(
            nodeGuid, node->getName(),
            static_cast<int>(ModelTypes::Object),
            project->getProjectGuid(),
            project->getProjectGuid(),
            QString(),
            QString(),
            QByteArray(),
            QJsonDocument(props).toJson(),
            QByteArray(),
            QByteArray()
        );
    }

    QString tileGuid;
    node->setMaterial(createMaterial(db, project, &tileGuid));
    if (realProject && !tileGuid.isEmpty()) {
        db->createDependency(
            static_cast<int>(ModelTypes::Object),
            static_cast<int>(ModelTypes::Texture),
            nodeGuid, tileGuid,
            project->getProjectGuid()
        );
    }
    return node;
}

}   // namespace defaultfloor
