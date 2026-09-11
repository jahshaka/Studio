/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/resetmaterialcommand.h"

#include <QObject>
#include <QSqlQuery>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "irisgl/document/materials/material.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "services/assetdelete.h"
#include "services/projectassets.h"

ResetMaterialCommand::ResetMaterialCommand(Database *db, Project *project,
                                           iris::MeshNodePtr meshNode,
                                           iris::MaterialPtr defaultMaterial,
                                           const QStringList &defaultTextures)
    : db(db), project(project), meshNode(meshNode),
      oldMaterial(meshNode ? meshNode->getMaterial() : iris::MaterialPtr()),
      defaultMaterial(defaultMaterial), defaultTextures(defaultTextures)
{
    setText(QObject::tr("Reset Material"));
}

void ResetMaterialCommand::redo()
{
    if (!meshNode) return;
    meshNode->setMaterial(defaultMaterial);

    droppedEdges.clear();
    releasedPins.clear();
    const QString projectGuid = project ? project->getProjectGuid() : QString();
    if (!db || projectGuid.isEmpty()) return;
    const QString nodeGuid = meshNode->getGUID();

    // What the node USES, as the catalog records it: an applied material asset
    // (Object -> Material, applyMaterialAsset), a graph material picked in the
    // panel (Object -> Shader), textures bound to its slots (Object ->
    // Texture). Every one of them but the default's own textures stops being
    // used by this node.
    QStringList applied;
    {
        QSqlQuery query;
        query.prepare("SELECT depender_type, dependee_type, dependee FROM dependencies "
                      "WHERE depender = ? AND project_guid = ?");
        query.addBindValue(nodeGuid);
        query.addBindValue(projectGuid);
        if (query.exec()) {
            while (query.next()) {
                const Edge edge{ query.value(0).toInt(), query.value(1).toInt(),
                                 query.value(2).toString() };
                if (defaultTextures.contains(edge.dependee)) continue;
                droppedEdges.append(edge);
                if (edge.dependeeType == static_cast<int>(ModelTypes::Material)
                    || edge.dependeeType == static_cast<int>(ModelTypes::Shader))
                    applied.append(edge.dependee);
            }
        }
    }
    for (const Edge &edge : droppedEdges) db->deleteDependency(nodeGuid, edge.dependee);

    // The default's own textures are what the node uses now (delete-then-create:
    // the table has no unique key on the pair).
    for (const QString &texture : defaultTextures) {
        db->deleteDependency(nodeGuid, texture);
        db->createDependency(static_cast<int>(ModelTypes::Object),
                             static_cast<int>(ModelTypes::Texture), nodeGuid, texture, projectGuid);
    }

    // An applied material nothing else in this project uses leaves the project
    // (the tray rule's USE — services/assettray.h): its pin is released, the
    // library row untouched. Only a PIN is released — a row the project owns
    // outright is left alone — and never an unlisted row, whose last pin going
    // would delete it for good (assetdelete::removeFromProject reaps those),
    // which undo could not bring back.
    applied.removeDuplicates();
    for (const QString &asset : applied) {
        if (!db->fetchDependers(asset, projectGuid).isEmpty()) continue;
        if (!db->isAssetPinnedBy(projectGuid, asset)) continue;
        const AssetRecord record = db->fetchAsset(asset);
        if (record.guid.isEmpty() || !record.listed) continue;
        if (assetdelete::removeFromProject(db, asset, projectGuid).ok) releasedPins.append(asset);
    }
}

void ResetMaterialCommand::undo()
{
    if (!meshNode) return;
    meshNode->setMaterial(oldMaterial);

    const QString projectGuid = project ? project->getProjectGuid() : QString();
    if (!db || projectGuid.isEmpty()) return;
    for (const QString &asset : releasedPins)
        ProjectAssets::addToProject(asset, db, project, ProjectAssets::AddKind::Binding);
    const QString nodeGuid = meshNode->getGUID();
    for (const Edge &edge : droppedEdges) {
        db->deleteDependency(nodeGuid, edge.dependee);
        db->createDependency(edge.dependerType, edge.dependeeType, nodeGuid, edge.dependee,
                             projectGuid);
    }
    releasedPins.clear();
    droppedEdges.clear();
}
