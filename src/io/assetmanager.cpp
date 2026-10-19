/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "io/assetmanager.h"

#include <QCoreApplication>
#include <vector>

namespace {

/// The owner and its view, in step (assetmanager.h).
struct Registry
{
    std::vector<std::unique_ptr<Asset>> owned;
    QVector<Asset*> view;
};

Registry &registry()
{
    static Registry r;
    return r;
}

}   // namespace

const QVector<Asset*> &AssetManager::getAssets()
{
    return registry().view;
}

Asset *AssetManager::addAsset(std::unique_ptr<Asset> asset)
{
    // The first registration ties the registry's end to the application's:
    // the assets are destroyed inside ~QCoreApplication, never after it.
    static const bool tied = [] {
        if (QCoreApplication::instance()) qAddPostRoutine(&AssetManager::clearAssetList);
        return true;
    }();
    Q_UNUSED(tied);
    Registry &r = registry();
    Asset *raw = asset.get();
    r.owned.push_back(std::move(asset));
    r.view.append(raw);
    return raw;
}

int AssetManager::removeAssets(const QString &guid)
{
    Registry &r = registry();
    int removed = 0;
    for (int i = int(r.owned.size()) - 1; i >= 0; --i) {
        if (r.owned[size_t(i)]->assetGuid != guid) continue;
        r.view.remove(i);
        r.owned.erase(r.owned.begin() + i);
        ++removed;
    }
    return removed;
}

void AssetManager::clearAssetList()
{
    Registry &r = registry();
    r.view.clear();
    r.view.squeeze();
    r.owned.clear();
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
