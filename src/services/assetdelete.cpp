/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetdelete.h"
#include "services/memberstamp.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>

#include "data/database/database.h"
#include "services/assetstorepaths.h"
#include "io/assetmanager.h"
#include "services/assethelper.h"
#include "services/imagematerial.h"
#include "services/projectmembership.h"

namespace assetdelete
{

QVector<AssetPinRecord> pins(Database *db, const QString &guid)
{
    if (!db) return {};
    return db->fetchAssetPins(guid);
}

QVector<AssetPinRecord> livePins(Database *db, const QString &guid)
{
    QVector<AssetPinRecord> live;
    for (const AssetPinRecord &pin : pins(db, guid))
        if (pin.live) live.push_back(pin);
    return live;
}

Outcome remove(Database *db, const QString &guid, bool keepShared, bool force)
{
    Outcome out;
    if (!db) {
        out.error = QStringLiteral("no library database in this session");
        return out;
    }
    if (db->fetchAsset(guid).guid.isEmpty()) {
        out.error = QStringLiteral("no asset with guid '%1'").arg(guid);
        return out;
    }

    out.pinCount = db->countAssetPins(guid);

    // ---- the pinned case: unlist, and touch NOTHING else ------------------
    if (!force && out.pinCount > 0) {
        out.unlisted = true;
        out.ok = db->setAssetListed(guid, false);
        if (!out.ok)
            out.error = QStringLiteral("the database refused to unlist '%1' — the asset is "
                                       "still in the library").arg(guid);
        return out;
    }

    // ---- the real delete --------------------------------------------------
    bool ok = true;
    if (keepShared) {
        // Conservative: only the asset row and its dependency links go;
        // dependee assets (possibly shared) stay.
        ok = db->deleteAsset(guid, force) && ok;
        ok = db->deleteDependency(guid) && ok;
        for (const auto &dep : db->fetchAssetGUIDAndDependencies(guid, false))
            ok = db->deleteDependency(guid, dep) && ok;
    } else {
        // THE LIBRARY DELETE TAKES ONLY BORN-INSIDE MEMBERS, NEVER A USER-IMPORTED
        // ROW (bundles audit G4, the library half; BUNDLE-P4). It used to be
        // deleteAssetAndDependencies — every asset of the recursive closure,
        // with no depender check — so deleting an avatar deleted the user's
        // MODEL (one that pre-existed "Create Avatar") and any clip SHARED with
        // another avatar. A member goes only when (a) it was born inside the set
        // being removed — a picked texture carrying the bundle's member/origin
        // stamp, a baked map whose PARENT is in the set, an image's companion
        // material — (b) no depender remains OUTSIDE the set, and (c) no living
        // project pins it. Every other row of the closure keeps its row and
        // loses only the edge from the dying asset.
        const QStringList closure = db->fetchAssetGUIDAndDependencies(guid, /*appendSelf*/ true);
        const QSet<QString> set(closure.begin(), closure.end());
        QStringList dying;
        dying.append(guid);
        for (const QString &member : closure) {
            if (member == guid) continue;
            const AssetRecord row = db->fetchAsset(member);
            if (row.guid.isEmpty()) continue;
            const QJsonObject props = QJsonDocument::fromJson(row.properties).object();
            bool bornInside = false;
            if (memberstamp::isStamped(row.properties)
                && set.contains(memberstamp::originOf(row.properties)))
                bornInside = true;
            if (!row.parent.isEmpty() && set.contains(row.parent)) bornInside = true;
            if (row.type == static_cast<int>(ModelTypes::Material)) {
                const QJsonObject def = QJsonDocument::fromJson(row.asset).object();
                if (set.contains(def.value(QStringLiteral("companionOf")).toString()))
                    bornInside = true;
            }
            if (!bornInside) continue;
            if (!force && db->countAssetPins(member) > 0) continue;   // a project holds it
            bool heldFromOutside = false;
            for (const QString &depender : db->hasMultipleDependers(member)) {
                if (depender == member || set.contains(depender)) continue;
                heldFromOutside = true;
                break;
            }
            if (heldFromOutside) continue;
            dying.append(member);
        }
        DbTransaction tx(QSqlDatabase::database());
        for (const QString &g : dying) {
            ok = db->deleteAsset(g, force) && ok;
            ok = db->deleteDependency(g) && ok;
            for (const auto &dep : db->fetchAssetGUIDAndDependencies(g, false))
                ok = db->deleteDependency(g, dep) && ok;
        }
        ok = tx.commit() && ok;
    }

    out.ok = ok;
    if (!ok) {
        out.error = QStringLiteral("the database refused the delete of '%1' — the asset is "
                                   "still in the library").arg(guid);
        return out;
    }

    // Whatever the retired legacy per-guid view left behind goes with the row;
    // the CONTENT under objects/ is assets.gc's to reclaim — only it can tell a
    // shared object from an exclusive one.
    QDir legacy(AssetStorePaths::legacyFolder(guid));
    if (legacy.exists()) legacy.removeRecursively();
    return out;
}


Outcome removeFromProject(Database *db, const QString &guid, const QString &projectGuid)
{
    Outcome out;
    if (!db) { out.error = QStringLiteral("no library database in this session"); return out; }
    if (projectGuid.isEmpty()) { out.error = QStringLiteral("no project is open"); return out; }
    const AssetRecord record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        out.error = QStringLiteral("no asset with guid '%1'").arg(guid);
        return out;
    }
    // THE WHOLE CLOSURE, judged against what is left (bundles audit G4). ADD is
    // recursive — `AssetHelper::fetchAssetAndAllDependencies`, which pins an
    // avatar's clips, its model, that model's mesh and its textures — and this
    // walked ONE LEVEL, so every depth-2 member stayed pinned forever and rode
    // every archive the project ever produced. It is the same recursive set
    // now, and a member goes only when no depender REMAINS OUTSIDE the set
    // being removed: a texture two pinned models share keeps its pin, and a
    // mesh whose only depender is the model going with it does not.
    const QStringList members = AssetHelper::fetchAssetAndAllDependencies(guid, db);
    QStringList toUnpin;
    for (const QString &member : members) {
        if (member == guid) { toUnpin.append(member); continue; }
        bool heldFromOutside = false;
        for (const QString &depender : db->hasMultipleDependers(member)) {
            if (depender == member || members.contains(depender)) continue;
            heldFromOutside = true;
            break;
        }
        if (heldFromOutside) continue;
        toUnpin.append(member);
    }

    // THE AUTO-MINTED COMPANION goes with the image (owner's open question,
    // MASTER_QUEUE §26; lead call 2026-09-10: YES). Adding an IMAGE to a
    // project mints a companion PBR material for it and pins that too
    // (ProjectAssets::addToProject — IMAGE_PLANE_SPEC §8.1), so removing the
    // image while leaving the material leaves the user a material tile in the
    // bin for a picture that is no longer in the project — a leftover they
    // never asked for and cannot explain. It is symmetry: the add created it,
    // the remove takes it back out.
    //
    // Only ever the one WE minted, and only while nothing in the project can
    // still be using it:
    //   * it carries the mint's stamp (ImageMaterial::companionMaterials —
    //     `companionOf: <this texture>`, written only by createMaterialAsset).
    //     Identity, not shape: the old shape test ("a Material whose only
    //     dependee is this texture") matched a material the USER authored on
    //     the same image just as well — and since addToProject mints nothing
    //     when any material already depends on the texture, theirs is exactly
    //     what stands in the companion's place (code review 2026-09-10);
    //   * THIS project pins it — the row's existence, since a companion is a
    //     DB-only asset whose pin carries an empty oid, so AssetCas::pinnedOid
    //     cannot tell "no pin" from "no bytes";
    //   * nothing DEPENDS on it: a companion applied to an object in this
    //     project is named by an Object -> Material edge, and that edge is what
    //     keeps it. (The scene blob cannot answer this — writeSceneNodeMaterial
    //     inlines a material's VALUES into the node and never names the asset
    //     guid, so the guard that read it never fired.)
    // The LIBRARY row is never touched — this is a project-side remove — so
    // the worst case of a wrong guess is a pin the user re-adds with one drag.
    if (static_cast<ModelTypes>(record.type) == ModelTypes::Texture) {
        for (const QString &companion : ImageMaterial::companionMaterials(guid)) {
            if (toUnpin.contains(companion)) continue;
            if (!db->isAssetPinnedBy(projectGuid, companion)) continue;
            if (!db->hasMultipleDependers(companion).isEmpty()) continue;  // something rides it
            toUnpin.append(companion);
        }
    }
    bool ok = true;
    for (const QString &member : toUnpin) {
        if (!db->unpinAsset(projectGuid, member)) { ok = false; continue; }
        ++out.pinCount;
        // Session records belong to the OPEN project only, so a scrub is
        // never wrong here (the library delete scrubs the same way).
        auto &assets = AssetManager::getAssets();
        for (int i = assets.count() - 1; i >= 0; --i) {
            if (assets[i]->assetGuid != member) continue;
            delete assets[i];
            assets.remove(i);
        }
        // An UNLISTED row that just lost its last pin exists for nobody: reap
        // it the way deleteProject reaps its orphans.
        const AssetRecord m = member == guid ? record : db->fetchAsset(member);
        if (!m.guid.isEmpty() && !m.listed && db->countAssetPins(member) == 0) {
            if (db->deleteAsset(member, /*force*/ true)) out.unlisted = true;
            else ok = false;
        }
    }
    out.ok = ok;
    if (!ok) out.error = QStringLiteral("the database refused to drop a pin on '%1'").arg(guid);
    if (!toUnpin.isEmpty()) ProjectMembership::instance()->announce(projectGuid);
    return out;
}

} // namespace assetdelete
