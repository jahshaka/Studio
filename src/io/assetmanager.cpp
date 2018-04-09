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

void AssetManager::clearAssetList()
{
    assets.clear();
    assets.squeeze();
}
