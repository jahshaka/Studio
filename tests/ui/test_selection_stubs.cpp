/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// Link stubs for ui.selection_cost, ON TOP OF tests/ui/test_stubs.cpp (which
// this suite also compiles). The suite builds the WHOLE properties panel — all
// seventeen blades — so it reaches a few more corners of the Sql/io/dialog
// layers than the two single-panel suites do. Everything stubbed here is
// reachable only through a user gesture the benchmark never makes (picking an
// asset from the library, writing a sky or a particle system back to the
// database, adding an asset to the project); the panel construction and the
// setSceneNode() path this suite measures touch none of them.

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QVariant>
#include <QVector>

#include "data/database/database.h"
#include "io/scenewriter.h"
#include "services/assetmetadata.h"
#include "services/projectassets.h"
#include "services/sceneeditservice.h"
#include "ui/controls/libraryassetpicker.h"

QVector<AssetRecord> Database::fetchAssetsByType(const int &, const QString &)
{
    return QVector<AssetRecord>();
}

QByteArray Database::fetchAssetData(const QString &) const
{
    return QByteArray();
}

bool Database::updateAssetProperties(const QString &, const QByteArray &)
{
    return false;
}

bool Database::updateGlobalDependencyDependee(const int &, const QString &, const QString &)
{
    return false;
}

bool Database::checkIfRecordExists(const QString &, const QVariant &, const QString &, bool,
                                   const QString &)
{
    return false;
}

QJsonObject SceneWriter::jsonColor(QColor)
{
    return QJsonObject();
}

void SceneWriter::writeParticleData(QJsonObject &, iris::ParticleSystemNodePtr)
{
}

bool SceneEditService::setDecalTexture(const iris::DecalNodePtr &, const QString &)
{
    return false;
}

ProjectAssets::Result ProjectAssets::addToProject(const QString &, Database *, Project *, AddKind)
{
    return ProjectAssets::Result();
}

QString LibraryAssetPicker::pick(ModelTypes, Database *, const QString &, QWidget *)
{
    return QString();
}

// The light panel's IES row backfills library metadata when a profile is bound.
// (The real AssetMetadata pulls in the whole import/probe stack, video decoding
// included, for a code path no benchmark switch reaches.)
QJsonObject AssetMetadata::ensure(Database *, const QString &, const QString &)
{
    return QJsonObject();
}
