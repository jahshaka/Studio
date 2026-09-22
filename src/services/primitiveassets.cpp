/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/primitiveassets.h"

#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSqlDatabase>

#include "data/database/database.h"
#include "data/primitives.h"
#include "irisgl/core/logger.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/import/meshbake.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/meshbakestore.h"

namespace PrimitiveAssets
{

namespace {

/// The held meshes — what `Mesh::pinLoadPaths` used to hold parses in. Keyed by
/// the seed's reserved guid, so the name form and the path form of one ask share
/// one entry. STRONG references, deliberately: this is a fixed, small set (16
/// meshes, a few hundred kilobytes of index and vertex data) that every world
/// stands on, and the weak cache it replaces is exactly what made closing a
/// world cost the next open a re-read.
QMutex &cacheLock()
{
    static QMutex m;
    return m;
}

QHash<QString, iris::MeshPtr> &cache()
{
    static QHash<QString, iris::MeshPtr> c;
    return c;
}

/// The registered library (setLibrary), for the preview bridges that are handed
/// none. Written once at startup, read on the UI thread.
Database *&library()
{
    static Database *db = nullptr;
    return db;
}

/// The row exists and its source bytes are in the store: the path to them.
/// (The seed half has its own copy — one function, two translation units, and it
/// is four lines: the alternative is a third TU for it.)
QString storedSource(QSqlDatabase conn, const QString &root, Database *db, const QString &guid)
{
    if (!db) return QString();
    const AssetRecord row = db->fetchAsset(guid);
    if (row.guid.isEmpty() && row.name.isEmpty()) return QString();
    return AssetCas::resolveSource(conn, root, guid);
}

}   // namespace

void setLibrary(Database *db)
{
    library() = db;
}

iris::MeshPtr mesh(const QString &nameOrSeedPath, Database *db)
{
    const primitives::Def *def = primitives::byName(nameOrSeedPath);
    if (!def) def = primitives::bySeedMesh(nameOrSeedPath);
    if (!def || !def->guid) return iris::MeshPtr();
    const QString guid = QString::fromLatin1(def->guid);

    {
        QMutexLocker locked(&cacheLock());
        const auto hit = cache().constFind(guid);
        if (hit != cache().constEnd()) return hit.value();
    }

    // RESOLVE ONLY. Seeding is `ensureSeeded`/`seedAll` (primitiveseed.cpp), and
    // it is a separate translation unit ON PURPOSE: it needs the whole import
    // pipeline, while this half needs the catalog and the bake reader. The shell
    // seeds the list once per library when it opens one, so by the time any
    // document, dock or verb asks here the rows are there; a binary that never
    // seeds (a preview-only test, a tool) gets an honest null instead of a link
    // dependency on the importer.
    if (!db) db = library();
    const QString source = storedSource(QSqlDatabase::database(), AssetStorePaths::root(),
                                       db, guid);
    if (source.isEmpty()) {
        irisLog("primitive: '" + QString::fromLatin1(def->name)
                + "' is not seeded in this library — nothing to draw");
        return iris::MeshPtr();
    }
    iris::BakedModelPtr baked = MeshBakeStore::load(source, guid);
    if (!baked || baked->meshes.isEmpty()) {
        irisLog("primitive: '" + QString::fromLatin1(def->name)
                + "' has no readable bake — nothing to draw");
        return iris::MeshPtr();
    }
    iris::MeshPtr made = baked->meshes.first();
    if (!made) return iris::MeshPtr();

    QMutexLocker locked(&cacheLock());
    // Another thread may have published the same mesh meanwhile; the one already
    // held wins so the sharing stays maximal (the same rule the retired parse
    // cache followed).
    const auto hit = cache().constFind(guid);
    if (hit != cache().constEnd()) return hit.value();
    cache().insert(guid, made);
    return made;
}

bool allSeeded(Database *db)
{
    if (!db) db = library();
    if (!db) return false;
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    for (const primitives::Def &def : primitives::all()) {
        if (!def.guid) continue;
        const QString source = storedSource(conn, root, db, QString::fromLatin1(def.guid));
        if (source.isEmpty()) return false;
        if (!MeshBakeStore::isFresh(conn, root, source, QString::fromLatin1(def.guid)))
            return false;
    }
    return true;
}

void clearCache()
{
    QMutexLocker locked(&cacheLock());
    cache().clear();
}

}   // namespace PrimitiveAssets
