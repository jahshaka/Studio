/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

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
#include "services/thumbnailmanager.h"

#include <QMutex>
#include <QMutexLocker>

// The two static caches are shared between the UI thread and the import
// pipeline's worker (ImportBatchRunner runs importer convert() off-thread) —
// every read/insert goes through this lock. QImage itself is fine off the
// GUI thread; the hazard was only the unguarded QHash mutation.
static QMutex sThumbnailCacheMutex;

QSharedPointer<Thumbnail> ThumbnailManager::createThumbnail(QString filename, int width, int height)
{
    // assumes file exist for now
    QFileInfo fileInfo(filename);
    auto lastModified = fileInfo.lastModified();
    auto hash = filename + QString::number(lastModified.toMSecsSinceEpoch()) + QString("-") + QString::number(width) + QString("-") + QString::number(height);

    QImage image;
    {
        QMutexLocker lock(&sThumbnailCacheMutex);
        if (ThumbnailManager::thumbnails.contains(hash)) {
            return ThumbnailManager::thumbnails[hash];
        }
        if (cachedImages.contains(filename))
            image = cachedImages[filename];
    }
    if (image.isNull()) {
        image = QImage(filename);
    }

    auto thumb = new Thumbnail;
    thumb->filePath		= filename;
    thumb->thumbSize	= QSize(width, height);
    thumb->originalSize = image.size();
    thumb->thumb		= new QImage(image.scaledToHeight(height, Qt::SmoothTransformation));

    auto thumbPtr = QSharedPointer<Thumbnail>(thumb);
    {
        QMutexLocker lock(&sThumbnailCacheMutex);
        ThumbnailManager::thumbnails.insert(hash, thumbPtr);
    }

    return thumbPtr;
}

void ThumbnailManager::cacheImage(QString filename, QImage image)
{
    QMutexLocker lock(&sThumbnailCacheMutex);
    cachedImages.insert(filename, image);
}

QHash<QString, QSharedPointer<Thumbnail>> ThumbnailManager::thumbnails = QHash<QString, QSharedPointer<Thumbnail>>();
QHash<QString, QImage> ThumbnailManager::cachedImages = QHash<QString, QImage>();
