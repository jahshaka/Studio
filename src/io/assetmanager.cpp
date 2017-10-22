#include "assetmanager.h"

QList<Asset*> AssetManager::assets;

QList<Asset*> &AssetManager::getAssets()
{
    return assets;
}

void AssetManager::addAsset(Asset *asset)
{
    assets.append(asset);
    //assetsByPath.insert(asset->path, asset);
}

Asset *AssetManager::getAssetByPath(QString absolutePath)
{
    for (auto asset : assets)
        if (asset->path == absolutePath)
            return asset;

    return nullptr;
}
