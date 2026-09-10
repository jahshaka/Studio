/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/clipboardresolver.h"
#include "io/clipboardformat.h"

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
using clipboardformat::ClipItem;

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

// ---------------------------------------------------------------------------
// PAYLOAD VALIDATION. Everything in an envelope is UNTRUSTED INPUT: the text
// arrives from another machine, another build, a chat window, or a text editor
// somebody typed in. Three of these fields are used to BUILD FILESYSTEM PATHS
// and one becomes a database key, so they are checked against their alphabets
// before a path exists — not after, and never by trusting that our own writer
// produced them.
//
// The concrete hole this closes: an `oid` of "../../../../home/u/.ssh/id_rsa"
// with an empty `ext` made AssetStorePaths::objectPathIn hand back a path
// OUTSIDE the store, QFileInfo::exists said yes, and registerAsset ingested
// that file into the CAS under a guid the payload also chose. A clipboard
// payload could exfiltrate any readable file into the library.

/// A content id is exactly 64 lowercase hex characters — the sha256 the store
/// names objects by. Nothing else can address an object, so nothing else is
/// accepted (an uppercase spelling is refused rather than folded: the store
/// lowercases on write, so a mixed-case oid never named one of our objects).
bool validOid(const QString &oid)
{
    if (oid.size() != 64) return false;
    for (const QChar c : oid) {
        const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                         (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
        if (!hex) return false;
    }
    return true;
}

/// An extension as the store writes them: lowercase alphanumerics, at most 8.
/// EMPTY IS LEGAL — a stored file may have no extension at all (a LICENSE, a
/// .mtl-less mesh) and objectPathIn then names the object by its oid alone.
bool validExt(const QString &ext)
{
    if (ext.isEmpty()) return true;
    if (ext.size() > 8) return false;
    for (const QChar c : ext) {
        const bool ok = (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                        (c >= QLatin1Char('0') && c <= QLatin1Char('9'));
        if (!ok) return false;
    }
    return true;
}

/// A display / staging file name with any path in it removed. "../../x" and
/// "/etc/passwd" both collapse to their last component; empty means the field
/// carried nothing usable and the caller falls back to the oid.
QString safeFileName(const QString &name)
{
    if (name.isEmpty()) return QString();
    // Backslashes first: on a POSIX box QFileInfo does not treat them as
    // separators, so "..\\..\\x" would survive fileName() intact.
    QString cleaned = name;
    cleaned.replace(QLatin1Char('\\'), QLatin1Char('/'));
    cleaned = QFileInfo(cleaned).fileName();
    // A name made only of dots is a directory reference, not a file name.
    bool onlyDots = !cleaned.isEmpty();
    for (const QChar c : cleaned) if (c != QLatin1Char('.')) { onlyDots = false; break; }
    if (onlyDots) return QString();
    return cleaned;
}

/// Why an entry was refused before anything was built from it. Empty = fine.
QString rejectReason(const ClipAsset &asset)
{
    if (!assetrefs::isGuidValue(asset.guid))
        return QStringLiteral("its id is not a guid");
    if (!asset.parent.isEmpty() && !assetrefs::isGuidValue(asset.parent))
        return QStringLiteral("its parent id is not a guid");
    // The row's TYPE is a ModelTypes value and becomes the catalog's type
    // column; an absent or negative one would register an Undefined row that
    // nothing can open.
    if (asset.typeId < 0)
        return QStringLiteral("it carries no asset type");
    if (safeFileName(asset.name).isEmpty())
        return QStringLiteral("it carries no usable name");
    for (const ClipFile &file : asset.files) {
        if (!validExt(file.ext))
            return QStringLiteral("a file extension is not a store extension");
        if (file.inlineData.isEmpty() && !validOid(file.oid))
            return QStringLiteral("a file names no valid content id");
        if (!file.inlineData.isEmpty() && !file.oid.isEmpty() && !validOid(file.oid))
            return QStringLiteral("a file names no valid content id");
    }
    return QString();
}

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
    // The hint is a PATH FROM THE PAYLOAD: absolute, existing, different from
    // ours, and identifying itself as the store the payload claims. A relative
    // path would resolve against this process's working directory, which is
    // whatever the app was launched from.
    if (!envelope.source.storeRoot.isEmpty() && !envelope.source.storeId.isEmpty() &&
        QDir::isAbsolutePath(envelope.source.storeRoot) &&
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

        // VALIDATE BEFORE ANYTHING IS BUILT FROM IT (see the note above the
        // validators). A refused entry is reported exactly like content we
        // cannot find — the paste refuses the items that needed it — because
        // from the caller's side that is the same outcome.
        const QString reject = rejectReason(asset);
        if (!reject.isEmpty()) {
            ClipboardMissing refused;
            refused.guid = it.key();
            refused.name = safeFileName(asset.name);
            refused.type = asset.type;
            refused.neededBy = QStringLiteral("refused: %1").arg(reject);
            report.missing.append(refused);
            if (report.error.isEmpty())
                report.error = QStringLiteral("the clipboard payload describes an asset this "
                                              "build will not accept (%1)").arg(reject);
            continue;
        }

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
            // Both branches build a path, so both use the CHECKED fields only:
            // the staged name is the payload's name with any path stripped
            // (index-prefixed, so two files cannot collide), and an object path
            // is only formed from a 64-hex oid and a store extension.
            const QString stagedName = safeFileName(file.name);
            if (!file.inlineData.isEmpty()) {                       // step 2
                if (!commit) {
                    // The dry run knows the bytes are here; it does not need
                    // them on disk to say so.
                    path = QStringLiteral(":inline:");
                } else if (staging && staging->isValid()) {
                    const QString name = !stagedName.isEmpty()
                                             ? stagedName
                                             : (validOid(file.oid) ? file.oid
                                                                   : QStringLiteral("payload"));
                    path = QDir(staging->path()).filePath(
                        QStringLiteral("%1-%2").arg(located.size()).arg(name));
                    QFile out(path);
                    if (out.open(QIODevice::WriteOnly)) out.write(file.inlineData);
                    else path.clear();
                }
            } else if (validOid(file.oid)) {
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
                if (assetrefs::isReservedGuid(dependee)) continue;
                if (!assetrefs::isGuidValue(dependee)) continue;   // payload input
                db->createDependency(typeOf(sourceGuid), typeOf(dependee),
                                     guid, localGuid(dependee), QString());
            }
        }
    }

    // ---- references OUTSIDE the closure ----------------------------------
    // An item may name a guid the envelope carries no closure entry for (a
    // hand-edited payload, a producer that dropped its assets block). Nothing
    // above visited it, so it is neither known nor missing — and it would land
    // as a dangling guid, the exact D5 hole (verifier, 2026-09-10). Every
    // needed guid the loop never saw is looked up here: in the catalog it is
    // known; otherwise it is MISSING like any other unfindable content.
    // Without a caller's restriction (clipboard.resolve describes the whole
    // payload) the needed set is every guid the items reference, walked the
    // same way the paste walks it.
    QSet<QString> referenced;
    if (!limitTo) {
        for (const ClipItem &item : envelope.items) {
            QStringList direct;
            if (item.kind == QLatin1String(clipboardformat::kind::node()))
                direct = assetrefs::collectAssetGuids(item.nodeObject());
            else if (item.kind == QLatin1String(clipboardformat::kind::asset()))
                direct << item.data.value(QStringLiteral("guid")).toString();
            QStringList frontier = direct;
            while (!frontier.isEmpty()) {
                const QString g = frontier.takeFirst();
                if (g.isEmpty() || referenced.contains(g)) continue;
                referenced.insert(g);
                const auto entry = envelope.assets.constFind(g);
                if (entry != envelope.assets.constEnd()) frontier << entry->dependencies;
            }
        }
    }
    const QSet<QString> &needed = limitTo ? *limitTo : referenced;
    {
        for (const QString &needed : needed) {
            if (envelope.assets.contains(needed)) continue;              // visited above
            if (needed.isEmpty() || assetrefs::isReservedGuid(needed)) continue;
            if (report.known.contains(needed) || report.imported.contains(needed)) continue;
            bool alreadyMissing = false;
            for (const auto &m : report.missing) if (m.guid == needed) { alreadyMissing = true; break; }
            if (alreadyMissing) continue;
            if (assetrefs::isGuidValue(needed) && !db->fetchAsset(needed).guid.isEmpty()) {
                report.known.append(needed);
                toPin.append(needed);
                continue;
            }
            ClipboardMissing hole;
            hole.guid = needed;
            hole.neededBy = QStringLiteral("referenced by an item but absent from the payload's assets");
            report.missing.append(hole);
        }
    }

    // ---- pins (outside undo, idempotent) -----------------------------------
    if (commit && project && !project->getProjectGuid().isEmpty()) {
        for (const QString &guid : toPin) {
            if (!assetrefs::isGuidValue(guid)) continue;           // payload input
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

    // The row is stamped with the CURRENT project, exactly as the import spine
    // stamps an import (assetimportservice.cpp:304) — a clipboard paste is an
    // import, and a row with no project guid is a row the Assets page files
    // differently from every other imported asset.
    const QString projectGuid = project ? project->getProjectGuid() : QString();
    // The row's INSERT can fail (SQLITE_BUSY from another connection, SQLITE_FULL)
    // while the object store and the commit still succeed — the dangling-guid
    // class the review named; a refused row is a failed import, nothing else.
    const QString row = db->createAssetEntry(
        asset.guid, safeFileName(asset.name), asset.typeId, asset.parent,
        projectGuid, QString(), QString(), QByteArray(),
        asset.properties, QByteArray(), asset.blob,
        asset.viewFilter >= 0 ? static_cast<AssetViewFilter>(asset.viewFilter)
                              : AssetViewFilter::AssetsView);
    if (row.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("the library refused the asset row");
        return false;   // the transaction rolls back with tx
    }

    for (const auto &pair : files) {
        QString oid;
        const QString name = safeFileName(pair.first.name);
        if (!AssetCas::ingestFile(conn, root, pair.second, asset.guid,
                                  pair.first.role.isEmpty() ? QStringLiteral("source")
                                                            : pair.first.role,
                                  name.isEmpty() ? QFileInfo(pair.second).fileName() : name,
                                  &oid, errorOut))
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
