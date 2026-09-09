/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/clipboardresolver.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QTemporaryDir>

#include <memory>

#include "data/database/database.h"
#include "data/project.h"
#include "io/assetrefs.h"
#include "irisgl/core/irisutils.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/projectassets.h"
#include "scripting/modules/moduleshared.h"

using clipboardformat::ClipAsset;
using clipboardformat::ClipFile;
using clipboardformat::Envelope;

ClipboardResolver::ClipboardResolver(Database *database, Project *proj)
    : db(database), project(proj)
{
}

ClipboardResolveReport ClipboardResolver::plan(const Envelope &envelope,
                                               const QSet<QString> *limitTo) const
{
    return run(envelope, false, limitTo);
}

ClipboardResolveReport ClipboardResolver::apply(const Envelope &envelope,
                                                const QSet<QString> *limitTo)
{
    return run(envelope, true, limitTo);
}

namespace {

/// Which item referenced which guid, for the missing report ("neededBy").
QHash<QString, QString> neededByMap(const Envelope &envelope)
{
    QHash<QString, QString> out;
    for (const auto &item : envelope.items) {
        if (item.kind == QLatin1String(clipboardformat::kind::node())) {
            const QJsonObject nodeObj = item.nodeObject();
            const QString name = nodeObj.value(QStringLiteral("name")).toString();
            for (const auto &ref : assetrefs::collectAssetRefs(nodeObj))
                if (!out.contains(ref.guid))
                    out.insert(ref.guid, QStringLiteral("%1 (%2)").arg(name, ref.key));
        } else if (item.kind == QLatin1String(clipboardformat::kind::asset())) {
            const QString guid = item.data.value(QStringLiteral("guid")).toString();
            if (!guid.isEmpty() && !out.contains(guid))
                out.insert(guid, item.displayName());
        }
    }
    return out;
}

} // namespace

ClipboardResolveReport ClipboardResolver::run(const Envelope &envelope, bool commit,
                                              const QSet<QString> *limitTo) const
{
    ClipboardResolveReport report;
    if (!db) { report.error = QStringLiteral("no library is open"); return report; }

    const QString localRoot = AssetStorePaths::root();
    const QHash<QString, QString> neededBy = neededByMap(envelope);

    // Step 4's precondition, computed ONCE: is the source store readable here
    // and is it the store it claims to be? Identity is the store id, never the
    // path (a copied library at a different path is a different store).
    QString siblingRoot;
    if (!envelope.source.storeRoot.isEmpty() && !envelope.source.storeId.isEmpty() &&
        envelope.source.storeRoot != localRoot && QDir(envelope.source.storeRoot).exists()) {
        QString siblingId;
        if (AssetCas::readStoreInfo(envelope.source.storeRoot, &siblingId, nullptr) &&
            siblingId == envelope.source.storeId)
            siblingRoot = envelope.source.storeRoot;
    }

    // Inline bytes land here — and ONLY for a commit: a dry run that wrote a
    // few megabytes of staged files to answer "what would happen" is a side
    // effect of a question.
    std::unique_ptr<QTemporaryDir> staging;
    if (commit) staging = std::make_unique<QTemporaryDir>();
    QStringList toPin;

    for (auto it = envelope.assets.constBegin(); it != envelope.assets.constEnd(); ++it) {
        ClipAsset asset = it.value();
        asset.guid = it.key();
        if (asset.guid.isEmpty() || assetrefs::isReservedGuid(asset.guid)) continue;
        // Only what the caller is going to use (see plan()'s note).
        if (limitTo && !limitTo->contains(asset.guid)) continue;

        // ---- step 1: known here, at the right type -------------------------
        const AssetRecord record = db->fetchAsset(asset.guid);
        if (!record.guid.isEmpty()) {
            const bool typeAgrees = asset.typeId < 0 || record.type == asset.typeId;
            if (typeAgrees) {
                report.known.append(asset.guid);
                toPin.append(asset.guid);
                continue;
            }
            // A TYPE COLLISION: this catalog holds that guid as something else.
            // The asset lands under a FRESH guid and every item that named the
            // old one is rewritten by the key-aware walk (never a text
            // replace). Practically this only happens with hand-authored ids.
            const QString fresh = IrisUtils::generateGUID();
            report.guidMap.insert(asset.guid, fresh);
            asset.guid = fresh;
        }

        // ---- steps 2-4: locate bytes for every file ------------------------
        QVector<QPair<ClipFile, QString>> located;
        bool anyUnresolved = false;
        for (const ClipFile &file : asset.files) {
            QString path;
            if (!file.inlineData.isEmpty()) {                       // step 2
                if (!commit) {
                    // The dry run knows the bytes are here; it does not need
                    // them on disk to say so.
                    path = QStringLiteral(":inline:");
                } else if (staging && staging->isValid()) {
                    const QString name = file.name.isEmpty()
                                             ? QStringLiteral("%1.%2").arg(file.oid, file.ext)
                                             : file.name;
                    path = QDir(staging->path()).filePath(
                        QStringLiteral("%1-%2").arg(located.size()).arg(name));
                    QFile out(path);
                    if (out.open(QIODevice::WriteOnly)) out.write(file.inlineData);
                    else path.clear();
                }
            } else if (!file.oid.isEmpty()) {
                const QString local = AssetStorePaths::objectPathIn(localRoot, file.oid, file.ext);
                if (QFileInfo::exists(local)) path = local;         // step 3
                else if (!siblingRoot.isEmpty()) {                  // step 4
                    const QString sibling =
                        AssetStorePaths::objectPathIn(siblingRoot, file.oid, file.ext);
                    if (QFileInfo::exists(sibling)) path = sibling;
                }
            }
            if (path.isEmpty()) { anyUnresolved = true; continue; }
            located.append({ file, path });
        }

        // A row with files, none of which resolved, is MISSING (step 5). A row
        // with NO files at all is a DB-only asset (a material is its blob) and
        // is perfectly importable.
        if (!asset.files.isEmpty() && located.isEmpty()) {
            ClipboardMissing missing;
            missing.guid = it.key();
            missing.name = asset.name;
            missing.type = asset.type;
            missing.size = asset.files.isEmpty() ? -1 : asset.files.first().size;
            missing.neededBy = neededBy.value(it.key());
            report.missing.append(missing);
            continue;
        }
        // Some files resolved and some did not (a companion .mtl left behind
        // by the budget, say): the asset still lands — its source bytes are
        // what a scene renders with — and the row simply carries fewer files.
        Q_UNUSED(anyUnresolved);

        report.importable.append(asset.guid);
        if (!commit) continue;

        QString error;
        if (!registerAsset(asset, located, &error)) {
            // A FAILED IMPORT IS A HOLE, and it has to be reported as one. It
            // was neither `known` nor `missing` before, so a node that
            // referenced this asset sailed through the missing-check and landed
            // with a DANGLING guid — the exact outcome D5 exists to prevent
            // (the scene reader resolves nothing and logs nothing; the user
            // gets an invisible object). Both halves are recorded: the guid
            // joins `missing` so the items needing it are refused, and the
            // error is carried up so the verb itself refuses.
            ClipboardMissing failed;
            failed.guid = it.key();
            failed.name = asset.name;
            failed.type = asset.type;
            failed.neededBy = QStringLiteral("import failed: %1").arg(
                error.isEmpty() ? QStringLiteral("unknown error") : error);
            report.missing.append(failed);
            if (report.error.isEmpty())
                report.error = QStringLiteral("could not import '%1': %2")
                                   .arg(asset.name,
                                        error.isEmpty() ? QStringLiteral("unknown error") : error);
            // The guid map entry (a type collision that minted a fresh guid) is
            // withdrawn too: nothing landed under it, so nothing may be
            // rewritten to point at it.
            report.guidMap.remove(it.key());
            continue;
        }
        report.imported.append(asset.guid);
        toPin.append(asset.guid);
    }

    // ---- dependency edges, after every row exists ---------------------------
    //
    // Second pass on purpose: an edge names two rows, and the catalog's own
    // closure walk (fetchAssetGUIDAndDependencies, which pins and archives
    // both ride) follows them — writing an edge to a row that is not there yet
    // would produce a closure with a hole in it.
    if (commit) {
        const auto localGuid = [&](const QString &guid) {
            return report.guidMap.value(guid, guid);
        };
        const auto typeOf = [&](const QString &guid) {
            const auto found = envelope.assets.constFind(guid);
            if (found != envelope.assets.constEnd() && found->typeId >= 0) return found->typeId;
            return db->fetchAsset(localGuid(guid)).type;
        };
        for (const QString &guid : report.imported) {
            // `guid` is the LOCAL guid; the envelope is keyed by the source's.
            QString sourceGuid = guid;
            for (auto m = report.guidMap.constBegin(); m != report.guidMap.constEnd(); ++m)
                if (m.value() == guid) sourceGuid = m.key();
            const auto entry = envelope.assets.constFind(sourceGuid);
            if (entry == envelope.assets.constEnd()) continue;
            for (const QString &dependee : entry->dependencies) {
                if (dependee.isEmpty() || assetrefs::isReservedGuid(dependee)) continue;
                db->createDependency(typeOf(sourceGuid), typeOf(dependee),
                                     guid, localGuid(dependee), QString());
            }
        }
    }

    // ---- pins (outside undo, idempotent) -----------------------------------
    if (commit && project && !project->getProjectGuid().isEmpty()) {
        for (const QString &guid : toPin) {
            const auto result = ProjectAssets::addToProject(guid, db, project,
                                                            ProjectAssets::AddKind::Binding);
            if (result.ok()) {
                for (const QString &pinned : result.pinnedGuids)
                    if (!report.pinned.contains(pinned)) report.pinned.append(pinned);
            }
        }
    }

    return report;
}

bool ClipboardResolver::registerAsset(const ClipAsset &asset,
                                      const QVector<QPair<ClipFile, QString>> &files,
                                      QString *errorOut) const
{
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();

    // The import spine's commit shape, minus the importers: one transaction
    // around the row + the dependency edges + the CAS ingests, sidecars after
    // it commits (invariant I2).
    DbTransaction tx(conn);

    db->createAssetEntry(asset.guid, asset.name, asset.typeId, asset.parent,
                         QString(), QString(), QString(), QByteArray(),
                         asset.properties, QByteArray(), asset.blob,
                         asset.viewFilter >= 0 ? static_cast<AssetViewFilter>(asset.viewFilter)
                                               : AssetViewFilter::AssetsView);

    for (const auto &pair : files) {
        QString oid;
        if (!AssetCas::ingestFile(conn, root, pair.second, asset.guid,
                                  pair.first.role.isEmpty() ? QStringLiteral("source")
                                                            : pair.first.role,
                                  pair.first.name, &oid, errorOut))
            return false;
    }

    if (!tx.commit()) {
        if (errorOut) *errorOut = QStringLiteral("could not commit the clipboard import");
        return false;
    }

    QString casError;
    AssetCas::writeSidecar(conn, root, asset.guid, &casError);
    return true;
}
