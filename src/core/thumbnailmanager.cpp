/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QCryptographicHash>
#include <QImage>
#include <QSharedPointer>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QHash>
#include <QDebug>
#include "thumbnailmanager.h"

QSharedPointer<Thumbnail> ThumbnailManager::createThumbnail(QString filename, int width, int height)
{
    // assumes file exist for now
    QFileInfo fileInfo(filename);
    auto lastModified = fileInfo.lastModified();
    auto hash = filename + QString::number(lastModified.toMSecsSinceEpoch()) + QString("-") + QString::number(width) + QString("-") + QString::number(height);

    if (ThumbnailManager::thumbnails.contains(hash)) {
        return ThumbnailManager::thumbnails[hash];
    }

    QImage image;
    if (cachedImages.contains(filename))
        image = cachedImages[filename];
    else {
        image = QImage(filename);
    }

    auto thumb = new Thumbnail;
    thumb->filePath		= filename;
    thumb->thumbSize	= QSize(width, height);
    thumb->originalSize = image.size();
    thumb->thumb		= new QImage(image.scaledToHeight(height, Qt::SmoothTransformation));

    auto thumbPtr = QSharedPointer<Thumbnail>(thumb);
    ThumbnailManager::thumbnails.insert(hash, thumbPtr);

    return thumbPtr;
}

void ThumbnailManager::cacheImage(QString filename, QImage image)
{
    cachedImages.insert(filename, image);
}

QHash<QString, QSharedPointer<Thumbnail>> ThumbnailManager::thumbnails = QHash<QString, QSharedPointer<Thumbnail>>();
QHash<QString, QImage> ThumbnailManager::cachedImages = QHash<QString, QImage>();
