/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/projectfolders.h"

#include <QSet>

#include "data/database/database.h"
#include "data/guidmanager.h"
#include "services/assetdelete.h"
#include "services/projectmembership.h"

namespace projectfolders {

namespace {

Result failure(const QString &message)
{
    Result result;
    result.error = message;
    return result;
}

/// The root is spelled two ways by callers — an empty string and the project's
/// own guid — because that is what the drawers hold in `selectedGuid`. One
/// spelling inside: the project guid, which is what the `folders`/`assets`
/// rows carry.
QString rootOr(const QString &projectGuid, const QString &folder)
{
    return folder.isEmpty() ? projectGuid : folder;
}

bool isFolderOf(Database *db, const QString &projectGuid, const QString &guid)
{
    if (guid.isEmpty() || guid == projectGuid) return false;
    const FolderRecord row = db->fetchFolder(guid);
    return !row.guid.isEmpty() && row.projectGuid == projectGuid;
}

/// Would filing `folderGuid` under `target` put it inside itself?
bool wouldCycle(Database *db, const QString &projectGuid, const QString &folderGuid,
                const QString &target)
{
    QString walk = target;
    int guard = 0;
    while (!walk.isEmpty() && walk != projectGuid && ++guard < 1000) {
        if (walk == folderGuid) return true;
        walk = db->fetchFolder(walk).parent;
    }
    return false;
}

bool nameTaken(Database *db, const QString &projectGuid, const QString &parent,
               const QString &name, const QString &exceptGuid)
{
    for (const FolderRecord &row : db->fetchChildFolders(parent, projectGuid)) {
        if (row.guid == exceptGuid) continue;
        if (row.name.compare(name, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

}   // namespace

bool isSystemFolder(const QString &name)
{
    // The two the editor resolves BY NAME through Database::ensureFolder.
    return name.compare(QStringLiteral("Systems"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("Presets"), Qt::CaseInsensitive) == 0;
}

QVector<Info> list(Database *db, const QString &projectGuid, const QString &parent)
{
    QVector<Info> out;
    if (!db || projectGuid.isEmpty()) return out;

    const QHash<QString, QString> pinFolders = db->fetchProjectPinFolders(projectGuid);
    QHash<QString, int> pinnedPerFolder;
    for (auto it = pinFolders.constBegin(); it != pinFolders.constEnd(); ++it)
        ++pinnedPerFolder[it.value()];

    // Every folder, or one parent's children. A whole-tree listing walks the
    // parents breadth-first from the root rather than selecting the table, so
    // a row orphaned by an older build (a folder whose parent is gone) is not
    // reported as if it were reachable.
    QStringList queue;
    queue << rootOr(projectGuid, parent);
    const bool wholeTree = parent.isEmpty();
    QSet<QString> seen;
    while (!queue.isEmpty()) {
        const QString current = queue.takeFirst();
        if (seen.contains(current)) continue;
        seen.insert(current);
        for (const FolderRecord &row : db->fetchChildFolders(current, projectGuid)) {
            Info info;
            info.guid = row.guid;
            info.name = row.name;
            info.parent = row.parent;
            info.count = db->fetchChildAssets(row.guid, projectGuid).size()
                       + pinnedPerFolder.value(row.guid);
            out.append(info);
            if (wholeTree) queue << row.guid;
        }
    }
    return out;
}

Result create(Database *db, const QString &projectGuid, const QString &name,
              const QString &parent, const QString &guid)
{
    if (!db) return failure(QStringLiteral("no database"));
    if (projectGuid.isEmpty()) return failure(QStringLiteral("no project is open"));
    const QString folderName = name.trimmed();
    if (folderName.isEmpty()) return failure(QStringLiteral("a folder needs a name"));
    if (isSystemFolder(folderName))
        return failure(QStringLiteral("'%1' is a folder the editor keeps for itself — "
                                      "choose another name").arg(folderName));

    const QString under = rootOr(projectGuid, parent);
    if (under != projectGuid && !isFolderOf(db, projectGuid, under))
        return failure(QStringLiteral("no folder '%1' in this project to create it under")
                           .arg(under));
    if (nameTaken(db, projectGuid, under, folderName, QString()))
        return failure(QStringLiteral("this folder already holds a '%1'").arg(folderName));

    Result result;
    result.guid = guid.isEmpty() ? GUIDManager::generateGUID() : guid;
    if (!db->createFolder(folderName, under, result.guid, projectGuid)) {
        return failure(QStringLiteral("the folder could not be written"));
    }
    result.ok = true;
    announce(projectGuid);
    return result;
}

Result rename(Database *db, const QString &projectGuid, const QString &guid,
              const QString &name)
{
    if (!db) return failure(QStringLiteral("no database"));
    if (projectGuid.isEmpty()) return failure(QStringLiteral("no project is open"));
    const QString folderName = name.trimmed();
    if (folderName.isEmpty()) return failure(QStringLiteral("a folder needs a name"));
    if (isSystemFolder(folderName))
        return failure(QStringLiteral("'%1' is a folder the editor keeps for itself — "
                                      "choose another name").arg(folderName));

    const FolderRecord row = db->fetchFolder(guid);
    if (row.guid.isEmpty() || row.projectGuid != projectGuid)
        return failure(QStringLiteral("no folder '%1' in this project").arg(guid));
    if (isSystemFolder(row.name))
        return failure(QStringLiteral("'%1' is a folder the editor keeps for itself and "
                                      "finds by name — it cannot be renamed").arg(row.name));
    if (nameTaken(db, projectGuid, row.parent, folderName, guid))
        return failure(QStringLiteral("this folder already holds a '%1'").arg(folderName));

    Result result;
    result.guid = guid;
    result.ok = db->renameFolder(guid, folderName);
    if (!result.ok) return failure(QStringLiteral("the folder could not be renamed"));
    announce(projectGuid);
    return result;
}

Result remove(Database *db, const QString &projectGuid, const QString &guid, bool keepContents)
{
    if (!db) return failure(QStringLiteral("no database"));
    if (projectGuid.isEmpty()) return failure(QStringLiteral("no project is open"));
    if (guid.isEmpty() || guid == projectGuid)
        return failure(QStringLiteral("the project root is not a folder you can delete"));

    const FolderRecord row = db->fetchFolder(guid);
    if (row.guid.isEmpty() || row.projectGuid != projectGuid)
        return failure(QStringLiteral("no folder '%1' in this project").arg(guid));
    if (isSystemFolder(row.name))
        return failure(QStringLiteral("'%1' is a folder the editor keeps for itself — "
                                      "it cannot be deleted").arg(row.name));

    const QHash<QString, QString> pinFolders = db->fetchProjectPinFolders(projectGuid);
    Result result;

    if (keepContents) {
        // EVERYTHING MOVES UP. Child folders first, then the rows filed here —
        // both kinds, through the one filer.
        for (const FolderRecord &child : db->fetchChildFolders(guid, projectGuid))
            db->setFolderParent(child.guid, row.parent);
        for (const AssetRecord &asset : db->fetchChildAssets(guid, projectGuid))
            db->setAssetParent(asset.guid, row.parent);
        for (auto it = pinFolders.constBegin(); it != pinFolders.constEnd(); ++it)
            if (it.value() == guid)
                db->setProjectAssetFolder(projectGuid, it.key(),
                                          row.parent == projectGuid ? QString() : row.parent);
    } else {
        // THE CONTENTS LEAVE THE PROJECT, by the one door a tile takes
        // (services/assetdelete.h): this project's pin and the members only it
        // uses. The LIBRARY row is never touched — that is the house law, and
        // it is why this is not `deleteFolderAndDependencies` (which the two
        // drawers used to call from a PROJECT view, deleting library rows).
        for (const FolderRecord &child : db->fetchChildFolders(guid, projectGuid))
            remove(db, projectGuid, child.guid, false);
        for (const AssetRecord &asset : db->fetchChildAssets(guid, projectGuid))
            assetdelete::removeFromProject(db, asset.guid, projectGuid);
        for (auto it = pinFolders.constBegin(); it != pinFolders.constEnd(); ++it)
            if (it.value() == guid) assetdelete::removeFromProject(db, it.key(), projectGuid);
    }

    result.ok = db->deleteFolder(guid);
    result.guid = guid;
    if (!result.ok) return failure(QStringLiteral("the folder could not be deleted"));
    announce(projectGuid);
    return result;
}

QString folderOf(Database *db, const QString &projectGuid, const QString &guid)
{
    if (!db || projectGuid.isEmpty() || guid.isEmpty()) return QString();
    if (isFolderOf(db, projectGuid, guid)) {
        const QString parent = db->fetchFolder(guid).parent;
        return parent == projectGuid ? QString() : parent;
    }
    if (db->isAssetPinnedBy(projectGuid, guid)) {
        const QString filed = db->fetchProjectPinFolders(projectGuid).value(guid);
        if (!filed.isEmpty()) return filed;
        // A row the project ALSO owns carries its filing in `parent` (an
        // archive import pins the project's own rows); fall through to it.
    }
    const AssetRecord row = db->fetchAsset(guid);
    if (row.projectGuid == projectGuid && row.parent != projectGuid) return row.parent;
    return QString();
}

bool file(Database *db, const QString &projectGuid, const QString &guid, const QString &folder)
{
    if (!db || projectGuid.isEmpty() || guid.isEmpty()) return false;
    const QString target = folder == projectGuid ? QString() : folder;

    if (isFolderOf(db, projectGuid, guid))
        return db->setFolderParent(guid, rootOr(projectGuid, target));

    bool wrote = false;
    // A row can be BOTH pinned and project-owned (an archive import pins the
    // project's own rows): write both, so the two never disagree.
    if (db->isAssetPinnedBy(projectGuid, guid))
        wrote = db->setProjectAssetFolder(projectGuid, guid, target);
    const AssetRecord row = db->fetchAsset(guid);
    if (!row.guid.isEmpty() && row.projectGuid == projectGuid)
        wrote = db->setAssetParent(guid, rootOr(projectGuid, target)) || wrote;
    return wrote;
}

Result moveTo(Database *db, const QString &projectGuid, const QStringList &guids,
              const QString &folderGuid)
{
    if (!db) return failure(QStringLiteral("no database"));
    if (projectGuid.isEmpty()) return failure(QStringLiteral("no project is open"));

    const QString target = folderGuid == projectGuid ? QString() : folderGuid;
    if (!target.isEmpty() && !isFolderOf(db, projectGuid, target))
        return failure(QStringLiteral("no folder '%1' in this project").arg(folderGuid));

    // EVERY ROW IS JUDGED BEFORE ANY ROW MOVES: a multi-move is one gesture and
    // one undo step, so it must not half-happen.
    const QSet<QString> pinned = db->fetchProjectPinnedGuids(projectGuid);
    QStringList accepted;
    for (const QString &guid : guids) {
        if (guid.isEmpty()) continue;
        if (accepted.contains(guid)) continue;

        if (isFolderOf(db, projectGuid, guid)) {
            if (guid == target)
                return failure(QStringLiteral("a folder cannot be moved into itself"));
            if (wouldCycle(db, projectGuid, guid, target))
                return failure(QStringLiteral("a folder cannot be moved inside itself"));
            accepted << guid;
            continue;
        }

        const AssetRecord row = db->fetchAsset(guid);
        if (row.guid.isEmpty())
            return failure(QStringLiteral("nothing in this project has the guid '%1'").arg(guid));
        // An IMPORT MEMBER is part of its asset, not a thing in a folder — its
        // `parent` names another ASSET, which is exactly the tray's rule 1
        // (services/assettray.h). Filing one would hide it from every listing.
        if (!row.parent.isEmpty() && row.parent != projectGuid
            && !db->fetchAsset(row.parent).guid.isEmpty())
            return failure(QStringLiteral("'%1' is part of another asset, not a tile in a "
                                          "folder").arg(row.name.isEmpty() ? guid : row.name));
        if (!pinned.contains(guid) && row.projectGuid != projectGuid)
            return failure(QStringLiteral("'%1' is not in this project")
                               .arg(row.name.isEmpty() ? guid : row.name));
        accepted << guid;
    }

    Result result;
    for (const QString &guid : accepted) {
        if (folderOf(db, projectGuid, guid) == target) continue;   // already there
        if (file(db, projectGuid, guid, target)) ++result.moved;
    }
    result.ok = true;
    if (result.moved > 0) announce(projectGuid);
    return result;
}

void announce(const QString &projectGuid)
{
    ProjectMembership::instance()->announce(projectGuid);
}

}   // namespace projectfolders
