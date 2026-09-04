/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "io/assetmanager.h"

QVector<Asset*> AssetManager::assets;

QVector<Asset*> &AssetManager::getAssets()
{
    return assets;
}

void AssetManager::addAsset(Asset *asset)
{
    assets.append(asset);
}

void AssetManager::replaceAssets(QString oldAssetGuid, Asset* asset)
{
	auto assetOld = getAssedByGuid(oldAssetGuid);
	if (!assetOld) {
		assets.append(asset);
		return;
	}
	// Re-registering the SAME object (a caller that looked the asset up,
	// mutated it and handed it back) must not destroy it.
	if (assetOld == asset) return;

	assets.removeOne(assetOld);
	delete assetOld;
	assets.append(asset);
}



void AssetManager::clearAssetList()
{
    // The list OWNS its assets (assetmanager.h). qDeleteAll before clear:
    // the derived Asset carries a QVariant payload — a SceneNodePtr for model
    // assets — so a bare clear() leaked the asset AND pinned its whole mesh
    // subtree for the life of the process.
    qDeleteAll(assets);
    assets.clear();
    assets.squeeze();
}

Asset* AssetManager::getAssedByGuid(QString guid)
{
	for (auto asset : AssetManager::getAssets()) {
		if (asset->assetGuid == guid) {
			return asset;
		}
	}

	return nullptr;
}