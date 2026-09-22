/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/libraryreset.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSqlQuery>

#include "app/firstrun.h"
#include "irisgl/core/logger.h"
#include "data/database/database.h"
#include "io/assetrefs.h"
#include "data/project.h"
#include "services/assetstore.h"
#include "services/assetstorepaths.h"
#include "services/import/importbatchrunner.h"
#include "services/materialpresetseeder.h"
#include "services/primitiveassets.h"
#include "services/thumbnailmanager.h"
#include "services/thumbnailrebuild.h"

namespace {

/// A staging temp, by the shape `FileWrite::stagingTempPath` writes
/// ("<final>.tmp-<pid>-<serial>") — the same test `assetgc` makes.
bool isStagingTemp(const QString &fileName)
{
    return fileName.contains(QLatin1String(".tmp-"));
}

/// Count the FILES under `dir` (recursively), splitting the staging temps out
/// of the total so the two numbers mean what they say.
void countFiles(const QString &dir, int *files, int *staging)
{
    if (!QDir(dir).exists()) return;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (staging && isStagingTemp(it.fileName())) { ++(*staging); continue; }
        if (files) ++(*files);
    }
}

/// Remove one path, WHATEVER IT IS, without ever following a symlink.
///
/// `QDir::removeRecursively` skips symlinks it meets INSIDE a tree but follows
/// one handed to it at the top — so a project folder (or a store entry) that
/// is itself a link would have had its TARGET emptied, which is somebody
/// else's data by definition. A link is unlinked; only a real directory is
/// walked.
bool removePath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) return true;
    if (info.isSymLink() || !info.isDir()) return QFile::remove(path);
    return QDir(path).removeRecursively();
}

/// A name the app itself minted a folder under: a bare UUID. The one test
/// (`assetrefs::isGuidValue`) both halves of this file use, because the whole
/// difference between "ours to delete" and "the user's, leave it" is whether
/// the app named it.
bool isOurGuidName(const QString &name)
{
    return assetrefs::isGuidValue(name);
}

/// EVERY STORED THUMBNAIL THE WIPE TAKES. They are BLOBS IN THE CATALOG, not
/// files in a cache directory: one per asset row and one per project row. (The
/// `thumbnails` TABLE is counted too and is always zero — nothing in this tree
/// writes it any more; it is dropped with the rest and is a CRUD candidate of
/// its own.)
int thumbnailCount(Database *db)
{
    if (!db) return 0;
    int total = 0;
    const auto count = [&total](const QString &sql) {
        QSqlQuery query;
        if (!query.exec(sql)) return;
        if (query.next()) total += query.value(0).toInt();
    };
    count(QStringLiteral("SELECT COUNT(*) FROM assets WHERE thumbnail IS NOT NULL "
                         "AND LENGTH(thumbnail) > 0"));
    count(QStringLiteral("SELECT COUNT(*) FROM projects WHERE thumbnail IS NOT NULL "
                         "AND LENGTH(thumbnail) > 0"));
    count(QStringLiteral("SELECT COUNT(*) FROM thumbnails"));
    return total;
}

}   // namespace

namespace libraryreset {

QVariantMap Removed::toMap() const
{
    QVariantMap out;
    out.insert(QStringLiteral("objects"), objects);
    out.insert(QStringLiteral("sidecars"), sidecars);
    out.insert(QStringLiteral("projects"), projects);
    out.insert(QStringLiteral("thumbnails"), thumbnails);
    out.insert(QStringLiteral("staging"), staging);
    return out;
}

QString refusalReason()
{
    if (ImportBatchRunner::anyRunning())
        return QStringLiteral("an import is running");
    if (MaterialPresetSeeder::instance().isRunning())
        return QStringLiteral("the first-run preset seed is running");
    if (thumbrebuild::sweepRunning())
        return QStringLiteral("a thumbnail rebuild is running");
    // AN OFFLINE STORE IS NOT AN EMPTY ONE. The root is a user-chosen path
    // (assets/storeRoot) and it may be an unmounted drive: wiping the catalog
    // while its bytes are unreachable would leave the whole store orphaned on
    // that drive with nothing left that names it — and the verb would have
    // answered ok with objects:0, which is a lie about what is on the disk.
    if (!AssetStoreService::online())
        return QStringLiteral("the asset store at %1 is offline — reconnect the drive or folder "
                              "it is on first").arg(AssetStorePaths::root());
    return QString();
}

Result reset(Database *db, SettingsManager *settings, const QString &projectsRoot,
             const std::function<QString(const QString &)> &folderForProject,
             bool seedPresets)
{
    Result result;
    if (!db) {
        result.error = QStringLiteral("there is no library in this session");
        return result;
    }
    const QString why = refusalReason();
    if (!why.isEmpty()) {
        result.error = why;
        return result;
    }

    // ---- 1. COUNT FIRST -------------------------------------------------
    // The numbers describe the library that WAS there. Counted before a byte
    // moves, because a count taken afterwards can only describe failures.
    const QString storeRoot = AssetStorePaths::root();
    countFiles(AssetStorePaths::objectsDir(), &result.removed.objects, &result.removed.staging);
    countFiles(AssetStorePaths::sidecarDir(), &result.removed.sidecars, &result.removed.staging);
    result.removed.thumbnails = thumbnailCount(db);

    // ---- 2. THE PROJECT FOLDERS ------------------------------------------
    // By each project's OWN recorded location (`projects.location`, SMALL-UI-A:
    // a project may live on another drive), which is what the resolver handed
    // in knows.
    const QVector<ProjectTileData> projects = db->fetchProjects(0);
    for (const ProjectTileData &row : projects) {
        const QString folder = folderForProject ? folderForProject(row.guid) : QString();
        if (folder.isEmpty()) continue;
        const QFileInfo info(folder);
        if (!info.exists() && !info.isSymLink()) continue;
        if (removePath(folder)) ++result.removed.projects;
    }

    // …and then the folders under the default root whose rows died in an
    // earlier life. ONLY THE ONES THIS APP NAMED: a project folder is always
    // `Projects/<guid>` (ProjectService::folderUnder), and the root this sits
    // under is the `default_directory` PREFERENCE — a free-form folder picker,
    // so a user who points it at ~/Documents would otherwise have
    // ~/Documents/Projects emptied of everything they ever put there. Anything
    // not named like a guid is somebody's own folder: left, and not counted.
    const QString defaultProjects = QDir(projectsRoot).filePath(QStringLiteral("Projects"));
    if (QDir(defaultProjects).exists()) {
        const QFileInfoList leftovers =
            QDir(defaultProjects).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : leftovers) {
            if (!isOurGuidName(entry.fileName())) continue;
            if (removePath(entry.absoluteFilePath())) ++result.removed.projects;
            else if (result.error.isEmpty())
                result.error = QStringLiteral("%1 could not be removed")
                                   .arg(entry.absoluteFilePath());
        }
    }

    // ---- 3. THE STORE'S OWN LAYOUT, AND NOTHING ELSE IN ITS ROOT ----------
    // The root is a path the USER chose (`assets/storeRoot`: Move Store
    // mkpaths and copies into any absolute directory, Use Existing with force
    // adopts any existing one), so it can be ~/Documents or the top of a USB
    // stick. A reset may therefore remove only what THIS APP puts there —
    // `objects/`, `sidecar/`, `derived/`, `store.json`, the staging temps
    // beside them and the legacy per-guid folders (AssetStorePaths' layout,
    // its header's list) — and must leave every other file and folder in that
    // directory exactly where it is. Never a wildcard.
    if (!storeRoot.isEmpty() && QDir(storeRoot).exists()) {
        const auto take = [&result](const QString &path) {
            const QFileInfo info(path);
            if (!info.exists() && !info.isSymLink()) return;
            if (!removePath(path) && result.error.isEmpty())
                result.error = QStringLiteral("%1 could not be removed").arg(path);
        };
        take(AssetStorePaths::objectsDir());
        take(QDir(storeRoot).filePath(QStringLiteral("sidecar")));
        take(QDir(storeRoot).filePath(QStringLiteral("derived")));
        take(AssetStorePaths::storeInfoPath());

        const QFileInfoList entries =
            QDir(storeRoot).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot
                                          | QDir::Hidden | QDir::System);
        for (const QFileInfo &entry : entries) {
            // A staging temp of one of the files above (`<final>.tmp-<pid>-<n>`
            // — store.json's is written right here in the root).
            if (!entry.isDir() && isStagingTemp(entry.fileName())) {
                ++result.removed.staging;
                take(entry.absoluteFilePath());
                continue;
            }
            // A legacy per-guid folder: the pre-CAS store's layout, still read
            // for assets that were never migrated. Its files are stored bytes,
            // so they count as objects.
            if (entry.isDir() && isOurGuidName(entry.fileName())) {
                countFiles(entry.absoluteFilePath(), &result.removed.objects,
                           &result.removed.staging);
                take(entry.absoluteFilePath());
            }
        }
    }

    // The in-memory picture cache goes with the files it scaled: its keys are
    // (path, mtime, size), and the next library will reuse those paths.
    ThumbnailManager::clearCache();

    // ---- 4. THE CATALOG: DROP THE TABLES, AND CREATE THEM AGAIN ----------
    // The second half is the one the old button left to a restart that never
    // happened — `wipeDatabase` DROPs, so a session that carried on ran on a
    // database with no tables in it at all.
    db->wipeDatabase();
    db->createAllTables();

    // ---- 5. THE FRESH-INSTALL BOOTSTRAP ----------------------------------
    // The store's own identity first: the root is recreated (it is the default
    // one, or a custom one that still exists) and given a NEW store.json, which
    // is what a first launch on an empty machine writes. A new storeId is the
    // truth — this is not the store that was here a moment ago.
    if (!storeRoot.isEmpty() && AssetStorePaths::root() == AssetStorePaths::defaultRoot())
        QDir().mkpath(storeRoot);
    AssetStoreService::bootstrapFromSettings(settings);

    // …and then the same first-run seed a launch runs, under the same rule
    // (shell/mainwindow.cpp): a DRIVEN session — a suite, a script, an MCP
    // client, the rig — seeds nothing, because a background seed landing
    // between two `assets.list` calls is a row count that moves under the
    // caller's feet. Those sessions have the seed on demand
    // (`materials.seedPresets`), exactly as they do at launch.
    //
    // AND NOT AT ALL WHEN THE CALLER IS ABOUT TO RESTART (`seedPresets`
    // false): the seed would start in a process that is already dying, its
    // worker would race the shutdown join, and the CHILD seeds the same
    // twenty presets at its own launch — two seeders over one store is how a
    // content-addressed library still ends up with two rows for one picture
    // (materialpresetseeder.h, "one importer at a time").
    if (seedPresets
        && (!FirstRun::isDrivenSession() || qEnvironmentVariableIsSet("JAHSHAKA_SEED_PRESETS")))
        MaterialPresetSeeder::instance().start(db);

    // THE PRIMITIVES, ALWAYS AND SYNCHRONOUSLY (ATOM P2,
    // services/primitiveassets.h). Unlike the material presets these are not a
    // warm-up: the twelve primitives, the Ground every new scene stands on and
    // the Teapot the samples name are BAKED LIBRARY ASSETS, and a catalog that
    // was just dropped and recreated holds none of them — so a reset left the
    // very next scene with a floor that had no geometry. It is not gated on the
    // driven-session rule for the same reason the launch seed is not: these rows
    // ARE the geometry, and a session that renders needs them whoever is driving.
    // The held meshes go with the catalog they came from: the objects those
    // MeshPtrs were read from have just been deleted.
    PrimitiveAssets::clearCache();
    QStringList seedErrors;
    PrimitiveAssets::seedAll(db, &seedErrors);
    for (const QString &line : seedErrors) {
        irisLog("library reset: primitive seed: " + line);
        if (result.error.isEmpty()) result.error = line;
    }

    result.ok = result.error.isEmpty();
    return result;
}

}   // namespace libraryreset
