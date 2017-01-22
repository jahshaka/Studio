/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef THUMBNAILMANAGER_H
#define THUMBNAILMANAGER_H

#include <QCryptographicHash>
#include <QImage>
#include <QSharedPointer>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QHash>
#include <QDebug>

class QImage;
class QCryptographicHash;

struct Thumbnail
{
    QImage* thumb;
    QString filePath;

    QSize thumbSize;
    QSize originalSize;
};


/**
 * This class caches thumbnails for image files
 * todo: find a way to remove unused thumbnails
 */
class ThumbnailManager
{
public:
    static QSharedPointer<Thumbnail> createThumbnail(QString filename, int width, int height);
    static QHash<QString, QSharedPointer<Thumbnail>> thumbnails;
};


#endif // THUMBNAILMANAGER_H
