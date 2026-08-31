/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef ASSETSTOREPATHS_H
#define ASSETSTOREPATHS_H

#include <QString>

/**
 * THE single authority for asset-store paths (ASSET_PIPELINE_SPEC §3.1.1).
 *
 * Every path under the asset store is derived HERE and nowhere else — the
 * phase-0 sweep replaced the ~27 hand-rolled AppData+"AssetStore" joins with
 * calls into this class.  The only exempt site is src/app/upgrader.cpp, which
 * wipes the PRE-storeRoot install location and must therefore always address
 * the DEFAULT root, never a relocated one.
 *
 * Layouts served:
 *   - legacy (pre-CAS):  <root>/<assetGuid>/<fileName>       (legacyFolder/legacyFilePath)
 *   - CAS (phase 2):     <root>/objects/ab/<oid>.<ext>        (objectPath)
 *                        <root>/sidecar/<assetGuid>.json      (sidecarPath)
 *                        <root>/derived/<cacheKey>/           (derivedPath)
 *                        <root>/store.json                    (storeInfoPath)
 *
 * The active root defaults to AppDataLocation + "/AssetStore" (exactly the
 * historical path — an absent setting means zero behavior change).  Phase 1
 * points it at the "assets/storeRoot" setting via setRootOverride(); tools
 * that operate on a copied library (migration rehearsal) use the same
 * override, or the explicit-root static variants.
 *
 * All joins normalize to forward slashes ('/' works on every Qt platform);
 * display formatting (native separators) is the UI's concern, not this
 * class's.  Oids are lowercased on entry so case-insensitive filesystems
 * (APFS, NTFS) can never alias two objects.
 */
class AssetStorePaths
{
public:
    // The compiled-in default: AppDataLocation + "/AssetStore".
    static QString defaultRoot();

    // The active store root. Equal to defaultRoot() unless overridden
    // (settings-backed from phase 1; rehearsal tools override explicitly).
    static QString root();

    // Process-wide root override. Empty string = back to defaultRoot().
    static void setRootOverride(const QString &rootPath);

    // --- Legacy per-guid layout (the pre-CAS store, and the phase-2
    //     read-fallback for not-yet-migrated assets) -----------------------
    static QString legacyFolder(const QString &assetGuid);
    static QString legacyFilePath(const QString &assetGuid, const QString &fileName);

    // Explicit-root variants (migration/verify/rebuild against a copied
    // library — ASSET_PIPELINE_PREFLIGHT §3.2 rehearsal support).
    static QString legacyFolderIn(const QString &rootPath, const QString &assetGuid);

    // --- Content-addressed layout (phase 2) ------------------------------
    static QString objectsDir();
    static QString objectPath(const QString &oid, const QString &ext);
    static QString objectPathIn(const QString &rootPath, const QString &oid, const QString &ext);
    static QString sidecarDir();
    static QString sidecarPath(const QString &assetGuid);
    static QString sidecarPathIn(const QString &rootPath, const QString &assetGuid);
    static QString derivedPath(const QString &cacheKey);
    static QString storeInfoPath();
    static QString storeInfoPathIn(const QString &rootPath);

private:
    static QString join(const QString &a, const QString &b);
    static QString sOverride;
};

#endif // ASSETSTOREPATHS_H
