/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// Link stubs for shadergraph.asset_widget_db. The widget under test reaches
// the Studio library, the Materials page's guid generator and the CAS; none of
// them is the subject, and linking them for real would drag half the app in.
// `fetchChildAssets` records the HANDLE it was called through — which is the
// whole question this suite asks.

#include <QString>
#include <QUuid>

#include "data/database/database.h"
#include "modules/materials/effectspage.h"
#include "services/assetcas.h"

Database::Database() = default;
Database::~Database() = default;

Database *gLastFetchChildAssetsHandle = nullptr;
int gFetchChildAssetsCalls = 0;

QVector<AssetRecord> Database::fetchChildAssets(const QString &, const QString &, int)
{
    gLastFetchChildAssetsHandle = this;
    ++gFetchChildAssetsCalls;
    return QVector<AssetRecord>();
}

// The project drawer lists the project's PINNED material bundles now
// (MATERIAL_BUNDLE_SPEC phase 1, the four-drawer rule) — this suite is about
// the handle the widget keeps, not the listing, so it answers empty.
QVector<AssetRecord> Database::fetchProjectPinnedAssets(const QString &)
{
    return QVector<AssetRecord>();
}
QStringList Database::fetchFolderNameByParent(const QString &) { return QStringList(); }
QStringList Database::fetchAssetNameByParent(const QString &) { return QStringList(); }
bool Database::createFolder(const QString &, const QString &, const QString &, const QString &,
                            bool)
{
    return false;
}
QString Database::createAssetEntry(const QString &, const QString &, const int &, const QString &,
                                   const QString &, const QString &, const QString &,
                                   const QByteArray &, const QByteArray &, const QByteArray &,
                                   const QByteArray &, const AssetViewFilter)
{
    return QString();
}

QString Database::createAssetEntry(const QString &, const QString &, const QString &, const int &,
                                   const QByteArray &, const QByteArray &, const AssetViewFilter)
{
    return QString();
}
bool Database::updateAssetAsset(const QString &, const QByteArray &) { return false; }
bool Database::updateAssetThumbnail(const QString &, const QByteArray &) { return false; }
bool Database::createDependency(const int &, const int &, const QString &, const QString &,
                                const QString &)
{
    return false;
}
bool Database::deleteDependency(const QString &, const QString &) { return false; }
bool Database::deleteAsset(const QString &, bool) { return false; }
bool Database::renameAsset(const QString &, const QString &) { return false; }
bool Database::hasDependencies(const QString &) { return false; }
QStringList Database::hasMultipleDependers(const QString &) { return QStringList(); }
QStringList Database::deleteFolderAndDependencies(const QString &, bool *) { return QStringList(); }
QStringList Database::deleteAssetAndDependencies(const QString &, bool *, bool) { return QStringList(); }
QStringList Database::fetchAssetGUIDAndDependencies(const QString &, bool) { return QStringList(); }
AssetRecord Database::fetchAsset(const QString &) { return AssetRecord(); }
QByteArray Database::fetchAssetData(const QString &) const { return QByteArray(); }

QString materials::EffectsPage::genGUID()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// A library tile dropped on the project drawer pins through ProjectAssets
// (MATERIAL_BUNDLE_SPEC 5); this suite never drops one.
#include "services/projectassets.h"
ProjectAssets::Result ProjectAssets::addToProject(const QString &, Database *, Project *, AddKind)
{
    return ProjectAssets::Result();
}
