#include "assetmanager.h"

QList<Asset*> AssetManager::assets;
QHash<QString, Asset*> AssetManager::nodes;

QList<Asset*> &AssetManager::getAssets()
{
    return assets;
}

void AssetManager::addAsset(Asset *asset)
{
    assets.append(asset);
}

QHash<QString, Asset*> AssetManager::getNodes()
{
	return nodes;
}

void AssetManager::addAsset(const QString &guid, Asset* asset)
{
	nodes.insert(guid, asset);
}