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
