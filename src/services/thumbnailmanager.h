/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef THUMBNAILMANAGER_H
#define THUMBNAILMANAGER_H

#include <QHash>
#include <QImage>
#include <QList>
#include <QSharedPointer>
#include <QSize>
#include <QString>

/// One cached thumbnail: the scaled picture plus what it was made from.
///
/// `thumb` is a QImage BY VALUE. It used to be a `QImage*` that nothing ever
/// deleted — Thumbnail has no destructor, and the cache below never dropped an
/// entry, so every distinct (file, mtime, size) triple leaked one heap QImage
/// for the life of the process (deep audit 2026-09, area 3). QImage is
/// implicitly shared: handing it out by value costs an atomic increment.
struct Thumbnail
{
    QImage  thumb;
    QString filePath;

    QSize thumbSize;
    QSize originalSize;
};


/// Scaled previews of image files on disk, cached by (path, mtime, size).
///
/// The cache is BOUNDED: least-recently-used entries are evicted once either
/// the entry count or the total pixel budget is exceeded. It used to be an
/// unbounded static QHash — "todo: find a way to remove unused thumbnails" —
/// which on a large library grew without limit for the life of the process.
class ThumbnailManager
{
public:
    static QSharedPointer<Thumbnail> createThumbnail(QString filename, int width, int height);

    /// Drops every cached thumbnail (project close, tests).
    static void clearCache();
    /// Live cache size — entries and bytes. For the cap gate.
    static int cachedCount();
    static qint64 cachedBytes();

    /// The caps. Public so the gate can assert against the same numbers the
    /// eviction uses instead of hard-coding a copy.
    static const int   kMaxEntries;
    static const qint64 kMaxBytes;

private:
    // Guarded by the mutex in the .cpp; `order` is the LRU chain, front =
    // least recently used.
    static QHash<QString, QSharedPointer<Thumbnail>> thumbnails;
    static QList<QString> order;
    static qint64 bytes;

    static void touchLocked(const QString &key);
    static void evictLocked();
};


#endif // THUMBNAILMANAGER_H
