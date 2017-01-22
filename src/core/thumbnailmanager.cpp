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
    auto hash = filename+lastModified.toMSecsSinceEpoch();

    if (ThumbnailManager::thumbnails.contains(filename)) {
        return ThumbnailManager::thumbnails[filename];
    }

    auto thumb = new Thumbnail;
    thumb->filePath = filename;

    QImage image(filename);
    thumb->thumbSize = QSize(width, height);
    thumb->originalSize = image.size();
    thumb->thumb = new QImage(image.scaled(thumb->thumbSize, Qt::KeepAspectRatioByExpanding));


    auto thumbPtr = QSharedPointer<Thumbnail>(thumb);
    ThumbnailManager::thumbnails.insert(filename, thumbPtr);

    return thumbPtr;
}

QHash<QString, QSharedPointer<Thumbnail>> ThumbnailManager::thumbnails =
        QHash<QString, QSharedPointer<Thumbnail>>();
