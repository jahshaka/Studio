/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "assetmanager.h"

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

	assets.removeOne(assetOld);
	assets.append(asset);
}



void AssetManager::clearAssetList()
{
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