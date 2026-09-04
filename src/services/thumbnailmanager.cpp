/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/thumbnailmanager.h"

#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>

// The cache is shared between the UI thread and the import pipeline's worker
// (ImportBatchRunner runs importer convert() off-thread) — every read, insert
// and eviction goes through this lock. QImage itself is fine off the GUI
// thread; the hazard was only the unguarded QHash mutation.
static QMutex sThumbnailCacheMutex;

QHash<QString, QSharedPointer<Thumbnail>> ThumbnailManager::thumbnails;
QList<QString> ThumbnailManager::order;
qint64 ThumbnailManager::bytes = 0;

// A 256x256 RGBA thumbnail is 256 KB, so 64 MB is ~256 of the largest ones the
// import pipeline asks for and several thousand of the 72x72 grid tiles. The
// entry cap catches the opposite shape: a huge library of tiny previews.
const int    ThumbnailManager::kMaxEntries = 512;
const qint64 ThumbnailManager::kMaxBytes   = 64ll * 1024 * 1024;

void ThumbnailManager::touchLocked(const QString &key)
{
    order.removeOne(key);
    order.append(key);          // back = most recently used
}

void ThumbnailManager::evictLocked()
{
    while (!order.isEmpty() &&
           (order.size() > kMaxEntries || bytes > kMaxBytes)) {
        const QString victim = order.takeFirst();
        auto it = thumbnails.find(victim);
        if (it == thumbnails.end()) continue;
        bytes -= (*it)->thumb.sizeInBytes();
        thumbnails.erase(it);
    }
    if (thumbnails.isEmpty()) bytes = 0;   // paranoia: no drift from rounding
}

QSharedPointer<Thumbnail> ThumbnailManager::createThumbnail(QString filename, int width, int height)
{
    // `width` is part of the cache key and of thumbSize, but NOT of the scale:
    // the picture has always been fitted to `height` with the aspect kept.
    // assumes file exist for now
    QFileInfo fileInfo(filename);
    auto lastModified = fileInfo.lastModified();
    auto hash = filename + QString::number(lastModified.toMSecsSinceEpoch()) + QString("-") + QString::number(width) + QString("-") + QString::number(height);

    {
        QMutexLocker lock(&sThumbnailCacheMutex);
        auto cached = thumbnails.constFind(hash);
        if (cached != thumbnails.constEnd()) {
            touchLocked(hash);
            return *cached;
        }
    }

    // Decode AT the thumbnail size. Reading the full image and scaling it
    // afterwards paid for every pixel of a 4K texture to produce a 72px tile;
    // QImageReader::setScaledSize lets the codec do it (libjpeg's scaled DCT,
    // and a single smooth scale inside the handler otherwise).
    // (No setAutoTransform: QImage(filename) did not apply the EXIF rotation
    // either, and turning it on here would silently re-orient every stored
    // thumbnail — a separate decision, not this lane's.)
    QImageReader reader(filename);
    const QSize full = reader.size();
    if (full.isValid() && full.height() > 0 && height > 0) {
        const int w = qMax(1, qRound(full.width() * double(height) / double(full.height())));
        reader.setScaledSize(QSize(w, height));
    }
    QImage image = reader.read();
    if (image.isNull()) {
        // Unreadable (or a format with no reader): keep the old contract —
        // a Thumbnail with a null picture, never a null pointer.
        image = QImage();
    }

    auto thumb = QSharedPointer<Thumbnail>::create();
    thumb->filePath     = filename;
    thumb->thumbSize    = QSize(width, height);
    thumb->originalSize = full.isValid() ? full : image.size();
    thumb->thumb        = image;

    {
        QMutexLocker lock(&sThumbnailCacheMutex);
        // Another thread may have produced the same key while we decoded.
        auto existing = thumbnails.constFind(hash);
        if (existing != thumbnails.constEnd()) {
            touchLocked(hash);
            return *existing;
        }
        thumbnails.insert(hash, thumb);
        bytes += thumb->thumb.sizeInBytes();
        touchLocked(hash);
        evictLocked();
    }

    // Returned by shared pointer, so an entry evicted a moment later stays
    // alive in the caller's hands.
    return thumb;
}

void ThumbnailManager::clearCache()
{
    QMutexLocker lock(&sThumbnailCacheMutex);
    thumbnails.clear();
    order.clear();
    bytes = 0;
}

int ThumbnailManager::cachedCount()
{
    QMutexLocker lock(&sThumbnailCacheMutex);
    return thumbnails.size();
}

qint64 ThumbnailManager::cachedBytes()
{
    QMutexLocker lock(&sThumbnailCacheMutex);
    return bytes;
}
