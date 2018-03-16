#include "assetmanager.h"

QList<Asset*> AssetManager::assets;

QList<Asset*> &AssetManager::getAssets()
{
    return assets;
}

void AssetManager::addAsset(Asset *asset)
{
    assets.append(asset);
}