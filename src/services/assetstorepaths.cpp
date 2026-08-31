/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "services/assetstorepaths.h"

#include <QDir>
#include <QStandardPaths>

QString AssetStorePaths::sOverride;

QString AssetStorePaths::join(const QString &a, const QString &b)
{
    // QDir::filePath handles the trailing-slash cases; cleanPath normalizes
    // separators so the same inputs always produce the same string (paths
    // get compared and used as cache keys).
    return QDir::cleanPath(QDir(a).filePath(b));
}

QString AssetStorePaths::defaultRoot()
{
    return join(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
                QStringLiteral("AssetStore"));
}

QString AssetStorePaths::root()
{
    return sOverride.isEmpty() ? defaultRoot() : sOverride;
}

void AssetStorePaths::setRootOverride(const QString &rootPath)
{
    // Stored with forward slashes (QSettings-portable — preflight §6.3);
    // cleanPath also strips any trailing separator.
    sOverride = rootPath.isEmpty() ? QString()
                                   : QDir::cleanPath(QDir::fromNativeSeparators(rootPath));
}

QString AssetStorePaths::legacyFolder(const QString &assetGuid)
{
    return legacyFolderIn(root(), assetGuid);
}

QString AssetStorePaths::legacyFolderIn(const QString &rootPath, const QString &assetGuid)
{
    return join(rootPath, assetGuid);
}

QString AssetStorePaths::legacyFilePath(const QString &assetGuid, const QString &fileName)
{
    return join(legacyFolder(assetGuid), fileName);
}

QString AssetStorePaths::objectsDir()
{
    return join(root(), QStringLiteral("objects"));
}

QString AssetStorePaths::objectPath(const QString &oid, const QString &ext)
{
    return objectPathIn(root(), oid, ext);
}

QString AssetStorePaths::objectPathIn(const QString &rootPath, const QString &oid, const QString &ext)
{
    // 2-char fan-out (DVC/git-annex); oid lowercased so APFS/NTFS can't alias.
    const QString hex = oid.toLower();
    QString name = hex;
    if (!ext.isEmpty()) name += QLatin1Char('.') + ext.toLower();
    return join(join(join(rootPath, QStringLiteral("objects")), hex.left(2)), name);
}

QString AssetStorePaths::sidecarDir()
{
    return join(root(), QStringLiteral("sidecar"));
}

QString AssetStorePaths::sidecarPath(const QString &assetGuid)
{
    return sidecarPathIn(root(), assetGuid);
}

QString AssetStorePaths::sidecarPathIn(const QString &rootPath, const QString &assetGuid)
{
    return join(join(rootPath, QStringLiteral("sidecar")), assetGuid + QStringLiteral(".json"));
}

QString AssetStorePaths::derivedPath(const QString &cacheKey)
{
    return join(join(root(), QStringLiteral("derived")), cacheKey);
}

QString AssetStorePaths::storeInfoPath()
{
    return storeInfoPathIn(root());
}

QString AssetStorePaths::storeInfoPathIn(const QString &rootPath)
{
    return join(rootPath, QStringLiteral("store.json"));
}
