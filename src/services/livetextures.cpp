/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/livetextures.h"

#include <QUuid>

#include "io/assetmanager.h"
#include "irisgl/document/assets/livetextures.h"
#include "irisgl/document/assets/texture2d.h"

QVector<LiveTextureCatalog::Record> &LiveTextureCatalog::rows()
{
    static QVector<Record> table;
    return table;
}

void LiveTextureCatalog::ensureRegistered()
{
    for (const Record &r : rows()) {
        if (AssetManager::getAssedByGuid(r.guid)) continue;
        auto *asset = new AssetLiveTexture;
        asset->assetGuid = r.guid;
        asset->fileName  = r.name;
        asset->path      = refFor(r.guid);
        AssetManager::addAsset(asset);
    }
}

QString LiveTextureCatalog::create(const QString &name, int width, int height, bool mipmaps,
                                   QString *error)
{
    auto refuse = [error](const QString &message) {
        if (error) *error = message;
        return QString();
    };
    if (width <= 0 || height <= 0)
        return refuse(QStringLiteral("a live texture needs a positive width and height"));
    // A ceiling rather than an out-of-memory: 8192 is the smallest maximum
    // 2D texture size any target we support guarantees, and w*h*4 bytes is
    // held on the CPU as well as in VRAM.
    if (width > 8192 || height > 8192)
        return refuse(QStringLiteral("a live texture is limited to 8192 x 8192"));

    const QString guid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    iris::Texture2DPtr tex = iris::LiveTextures::create(guid, name, width, height, mipmaps);
    if (!tex) return refuse(QStringLiteral("the live texture could not be created"));

    Record r;
    r.guid = guid;
    r.name = name.isEmpty() ? QStringLiteral("live texture") : name;
    r.width = width;
    r.height = height;
    r.mipmaps = mipmaps;
    rows().append(r);
    ensureRegistered();
    return guid;
}

bool LiveTextureCatalog::write(const QString &guid, const QImage &rgba, QString *error)
{
    auto refuse = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    iris::Texture2DPtr tex = iris::LiveTextures::find(guid);
    if (!tex) return refuse(QStringLiteral("no live texture '%1'").arg(guid));
    if (rgba.isNull()) return refuse(QStringLiteral("the image is empty"));
    if (rgba.width() != tex->getWidth() || rgba.height() != tex->getHeight())
        return refuse(QStringLiteral("live texture '%1' is %2x%3 and cannot resize — "
                                     "destroy it and create a %4x%5 one instead")
                          .arg(guid).arg(tex->getWidth()).arg(tex->getHeight())
                          .arg(rgba.width()).arg(rgba.height()));
    if (!tex->writeLive(rgba)) return refuse(QStringLiteral("the write was refused"));
    return true;
}

bool LiveTextureCatalog::exists(const QString &guid)
{
    return !iris::LiveTextures::find(guid).isNull();
}

QVariantMap LiveTextureCatalog::info(const QString &guid)
{
    QVariantMap out;
    iris::Texture2DPtr tex = iris::LiveTextures::find(guid);
    if (!tex) return out;
    QString name;
    for (const Record &r : rows()) if (r.guid == guid) name = r.name;
    out["guid"] = guid;
    out["name"] = name;
    out["width"] = tex->getWidth();
    out["height"] = tex->getHeight();
    out["mipmaps"] = tex->liveMipmaps();
    out["generation"] = double(tex->liveGeneration());
    out["ref"] = refFor(guid);
    return out;
}

bool LiveTextureCatalog::destroy(const QString &guid)
{
    if (!iris::LiveTextures::destroy(guid)) return false;
    for (int i = 0; i < rows().size(); ++i)
        if (rows()[i].guid == guid) { rows().remove(i); break; }
    // The AssetManager mirror row, if this session still has one. Nothing else
    // owns it, so it is deleted here rather than left as a name pointing at
    // pixels that no longer exist.
    if (Asset *asset = AssetManager::getAssedByGuid(guid)) {
        AssetManager::getAssets().removeOne(asset);
        delete asset;
    }
    return true;
}

QVector<LiveTextureCatalog::Record> LiveTextureCatalog::list()
{
    ensureRegistered();
    return rows();
}

QString LiveTextureCatalog::refFor(const QString &guid)
{
    return iris::LiveTextures::refFor(guid);
}

QString LiveTextureCatalog::guidOfRef(const QString &ref)
{
    return iris::LiveTextures::guidOf(ref);
}
