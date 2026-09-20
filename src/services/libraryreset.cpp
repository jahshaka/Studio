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
#include "data/database/database.h"
#include "data/project.h"
#include "services/assetstore.h"
#include "services/assetstorepaths.h"
#include "services/import/importbatchrunner.h"
#include "services/materialpresetseeder.h"
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

/// Remove everything INSIDE `dir` and leave the directory itself. Used for the
/// two roots a reset may never remove: the store root (the user chose where it
/// lives) and `<projectsRoot>/Projects`.
bool removeContents(const QString &dir)
{
    QDir d(dir);
    if (!d.exists()) return true;
    bool ok = true;
    const QFileInfoList entries =
        d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &entry : entries) {
        if (entry.isDir() && !entry.isSymLink())
            ok = QDir(entry.absoluteFilePath()).removeRecursively() && ok;
        else
            ok = QFile::remove(entry.absoluteFilePath()) && ok;
    }
    return ok;
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

QString busyReason()
{
    if (ImportBatchRunner::anyRunning())
        return QStringLiteral("an import is running");
    if (MaterialPresetSeeder::instance().isRunning())
        return QStringLiteral("the first-run preset seed is running");
    if (thumbrebuild::sweepRunning())
        return QStringLiteral("a thumbnail rebuild is running");
    return QString();
}

Result reset(Database *db, SettingsManager *settings, const QString &projectsRoot,
             const std::function<QString(const QString &)> &folderForProject)
{
    Result result;
    if (!db) {
        result.error = QStringLiteral("there is no library in this session");
        return result;
    }
    const QString why = busyReason();
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
    // in knows. Then whatever is still sitting under the default
    // `<projectsRoot>/Projects` — folders whose rows died in an earlier life.
    const QVector<ProjectTileData> projects = db->fetchProjects(0);
    for (const ProjectTileData &row : projects) {
        const QString folder = folderForProject ? folderForProject(row.guid) : QString();
        if (folder.isEmpty()) continue;
        QDir dir(folder);
        if (!dir.exists()) continue;
        if (dir.removeRecursively()) ++result.removed.projects;
    }
    const QString defaultProjects = QDir(projectsRoot).filePath(QStringLiteral("Projects"));
    if (QDir(defaultProjects).exists()) {
        const QStringList leftovers = QDir(defaultProjects)
                                          .entryList(QDir::AllEntries | QDir::NoDotAndDotDot
                                                     | QDir::Hidden | QDir::System);
        result.removed.projects += leftovers.size();
        if (!removeContents(defaultProjects))
            result.error = QStringLiteral("some project folders under %1 could not be removed")
                               .arg(defaultProjects);
    }

    // ---- 3. THE STORE'S CONTENTS, NEVER ITS ROOT --------------------------
    // objects/, sidecar/, derived/, store.json, the legacy per-guid folders
    // and the staging temps — all of it. The ROOT survives: the user may have
    // pointed the store at a folder of their own, and removing that folder
    // would take their choice with it (and, on a network or removable root,
    // could not be recreated at all).
    if (!storeRoot.isEmpty() && QDir(storeRoot).exists()) {
        if (!removeContents(storeRoot) && result.error.isEmpty())
            result.error = QStringLiteral("some of the asset store under %1 could not be removed")
                               .arg(storeRoot);
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
    if (!FirstRun::isDrivenSession() || qEnvironmentVariableIsSet("JAHSHAKA_SEED_PRESETS"))
        MaterialPresetSeeder::instance().start(db);

    result.ok = result.error.isEmpty();
    return result;
}

}   // namespace libraryreset
