/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/clipboardservice.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QSet>
#include <QUndoStack>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "data/settingsmanager.h"
#include "io/assetrefs.h"
#include "io/sceneformat.h"
#include "services/assetcas.h"
#include "services/assetclosure.h"
#include "services/assetstorepaths.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"
#include "services/undoservice.h"
#include "scripting/modules/moduleshared.h"

using clipboardformat::ClipAsset;
using clipboardformat::ClipItem;
using clipboardformat::Envelope;

namespace {

/// `clipboard/inlineLimitBytes` — the inline budget (D4: 4 MB by default).
/// A budget, not a limit on what can be copied: above it an asset travels by
/// content id and the resolver finds the bytes, or reports them missing.
constexpr qint64 kDefaultInlineLimit = 4 * 1024 * 1024;
const char *kInlineLimitSetting = "clipboard/inlineLimitBytes";

} // namespace

ClipboardService::ClipboardService(Database *database, Project *proj,
                                   SceneEditService *edit, SelectionService *sel,
                                   UndoService *undoService, ClipboardBackend *clipboardBackend,
                                   QObject *parent)
    : QObject(parent), db(database), project(proj), sceneEdit(edit), selection(sel),
      undo(undoService), backend(clipboardBackend)
{
    if (!backend) {
        // A session with no platform clipboard (a bare QCoreApplication, some
        // test hosts) still gets a working clipboard — an in-process one.
        if (SystemClipboardBackend::available()) backend = new SystemClipboardBackend;
        else                                    backend = new MemoryClipboardBackend;
    }
}

ClipboardService::~ClipboardService()
{
    delete backend;
}

iris::ScenePtr ClipboardService::scene() const
{
    return sceneEdit ? sceneEdit->scene() : iris::ScenePtr();
}

qint64 ClipboardService::inlineLimitBytes() const
{
    if (auto *settings = SettingsManager::getDefaultManager())
        return qMax(qint64(0), settings->getValue(QLatin1String(kInlineLimitSetting),
                                                  qint64(kDefaultInlineLimit)).toLongLong());
    return kDefaultInlineLimit;
}

void ClipboardService::setInlineLimitBytes(qint64 bytes)
{
    if (auto *settings = SettingsManager::getDefaultManager())
        settings->setValue(QLatin1String(kInlineLimitSetting), qMax(qint64(0), bytes));
}

QString ClipboardService::storeId() const
{
    const QString root = AssetStorePaths::root();
    QString id;
    if (AssetCas::readStoreInfo(root, &id, nullptr) && !id.isEmpty()) return id;
    // A STORE THAT EXISTS IDENTIFIES ITSELF. AssetStoreService writes store.json
    // at startup, but only when the root already exists — so a library created
    // during this session (the common case in a fresh install and in every test
    // run) has no identity until the NEXT launch, and until then "same library"
    // is unanswerable and the sibling-store fetch (§3.4 step 4) has nothing to
    // match on. Writing it here costs one small file, keeps an existing id
    // stable (writeStoreInfo's contract), and never CREATES the directory: a
    // dead mount point stays offline rather than becoming an empty store.
    if (!QDir(root).exists()) return QString();
    QString error;
    AssetCas::writeStoreInfo(root, &error);
    AssetCas::readStoreInfo(root, &id, nullptr);
    return id;
}

Envelope ClipboardService::newEnvelope() const
{
    Envelope envelope;
    envelope.version = clipboardformat::kVersion;
    envelope.app = Constants::CONTENT_VERSION;
    envelope.sceneFormat = sceneformat::kVersion;
    envelope.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    envelope.source.storeId = storeId();
    envelope.source.storeRoot = AssetStorePaths::root();
    if (project) envelope.source.projectGuid = project->getProjectGuid();
    return envelope;
}

ClipboardCopyResult ClipboardService::publish(const Envelope &envelope, int items)
{
    ClipboardCopyResult result;
    const QByteArray payload = envelope.toText();
    backend->setPayload(payload);
    cachedText = payload;
    cachedEnvelope = envelope;
    result.items = items;
    result.assets = envelope.assets.size();
    result.bytes = payload.size();
    return result;
}

// ---- copy ------------------------------------------------------------------

ClipboardCopyResult ClipboardService::copyNodes(const QList<iris::SceneNodePtr> &nodes)
{
    ClipboardCopyResult result;
    if (!sceneEdit || !scene()) {
        result.error = QStringLiteral("no scene is open");
        return result;
    }

    // The World root leaves BEFORE the D5 reduction (D6) — as a member it
    // would swallow the set, since every other node is its descendant.
    QList<iris::SceneNodePtr> input;
    for (const auto &node : nodes) if (!!node && !node->isRootNode()) input.append(node);

    Envelope envelope = newEnvelope();
    QVector<QJsonObject> nodeObjects;
    for (const auto &node : SceneEditService::effectiveSet(input)) {
        const SceneFragment fragment = sceneEdit->captureFragment(node);
        if (fragment.isNull()) continue;
        ClipItem item;
        item.kind = QLatin1String(clipboardformat::kind::node());
        // The fragment MINUS nodeIds: session identities mean nothing outside
        // the process that minted them (sceneformat.h says why they exist).
        item.data = QJsonObject{ { QStringLiteral("node"), fragment.node },
                                 { QStringLiteral("parent"), fragment.parentGuid },
                                 { QStringLiteral("index"), fragment.siblingIndex } };
        envelope.items.append(item);
        nodeObjects.append(fragment.node);
    }
    if (envelope.items.isEmpty()) {
        // An empty copy leaves the previous clipboard alone: Ctrl+C with
        // nothing selected must not throw away what you copied a moment ago.
        result.error = QStringLiteral("nothing to copy");
        return result;
    }

    assetclosure::Options options;
    options.inlineLimitBytes = inlineLimitBytes();
    options.storeRoot = AssetStorePaths::root();
    if (project) options.projectGuid = project->getProjectGuid();
    int inlined = 0, referenced = 0;
    envelope.assets = assetclosure::describe(assetclosure::forNodes(nodeObjects, db), db,
                                             options, &inlined, &referenced);

    result = publish(envelope, envelope.items.size());
    result.inlined = inlined;
    result.referenced = referenced;
    return result;
}

ClipboardCopyResult ClipboardService::copyAssets(const QStringList &guids)
{
    ClipboardCopyResult result;
    if (!db) { result.error = QStringLiteral("no library is open"); return result; }

    Envelope envelope = newEnvelope();
    QStringList seeds;
    for (const QString &guid : guids) {
        if (guid.isEmpty() || assetrefs::isReservedGuid(guid)) continue;
        const AssetRecord record = db->fetchAsset(guid);
        if (record.guid.isEmpty()) continue;
        ClipItem item;
        item.kind = QLatin1String(clipboardformat::kind::asset());
        item.data = QJsonObject{ { QStringLiteral("guid"), guid },
                                 { QStringLiteral("name"), record.name },
                                 { QStringLiteral("type"), scriptmod::assetTypeName(record.type) } };
        envelope.items.append(item);
        seeds << guid;
    }
    if (envelope.items.isEmpty()) {
        result.error = QStringLiteral("no such asset");
        return result;
    }

    assetclosure::Options options;
    options.inlineLimitBytes = inlineLimitBytes();
    options.storeRoot = AssetStorePaths::root();
    // A TILE carries its catalog row (name, type, placement, blob, properties):
    // pasting one into a library that has never seen the guid has to produce a
    // usable row, not an empty shell with the right id.
    options.includeRowBlobs = true;
    if (project) options.projectGuid = project->getProjectGuid();
    int inlined = 0, referenced = 0;
    envelope.assets = assetclosure::describe(assetclosure::expand(seeds, db), db,
                                             options, &inlined, &referenced);

    result = publish(envelope, envelope.items.size());
    result.inlined = inlined;
    result.referenced = referenced;
    return result;
}

// ---- reading ---------------------------------------------------------------

Envelope ClipboardService::contents() const
{
    const QByteArray payload = backend->payload();
    if (payload.isEmpty()) {
        cachedText.clear();
        cachedEnvelope = Envelope();
        return cachedEnvelope;
    }
    if (payload == cachedText) return cachedEnvelope;   // the cache (D3 b)
    cachedText = payload;
    cachedEnvelope = Envelope::fromText(payload);
    return cachedEnvelope;
}

QByteArray ClipboardService::text() const
{
    return backend->payload();
}

bool ClipboardService::setText(const QByteArray &payload, QString *errorOut)
{
    QString error;
    const Envelope envelope = Envelope::fromText(payload, &error);
    if (envelope.isNull()) {
        if (errorOut) *errorOut = error;
        return false;
    }
    backend->setPayload(payload);
    cachedText = payload;
    cachedEnvelope = envelope;
    if (errorOut) errorOut->clear();
    return true;
}

bool ClipboardService::sameLibrary(const Envelope &envelope) const
{
    const QString here = storeId();
    return !here.isEmpty() && here == envelope.source.storeId;
}

ClipboardResolveReport ClipboardService::resolve() const
{
    ClipboardResolver resolver(db, project);
    return resolver.plan(contents());
}

// ---- paste -----------------------------------------------------------------

namespace {

/// Every asset guid an item needs, including the closure the ENVELOPE records
/// (an object's textures are its dependencies, not the node's references).
QStringList neededGuids(const ClipItem &item, const Envelope &envelope)
{
    QStringList direct;
    if (item.kind == QLatin1String(clipboardformat::kind::node()))
        direct = assetrefs::collectAssetGuids(item.nodeObject());
    else if (item.kind == QLatin1String(clipboardformat::kind::asset()))
        direct << item.data.value(QStringLiteral("guid")).toString();

    QStringList out;
    QSet<QString> seen;
    QStringList frontier = direct;
    while (!frontier.isEmpty()) {
        const QString guid = frontier.takeFirst();
        if (guid.isEmpty() || seen.contains(guid)) continue;
        seen.insert(guid);
        out << guid;
        const auto entry = envelope.assets.constFind(guid);
        if (entry != envelope.assets.constEnd()) frontier << entry->dependencies;
    }
    return out;
}

} // namespace

ClipboardPasteResult ClipboardService::paste(const ClipboardPasteOptions &options)
{
    ClipboardPasteResult result;
    const Envelope envelope = contents();
    if (envelope.isNull()) {
        result.error = QStringLiteral("the clipboard does not hold Jahshaka content");
        return result;
    }

    // THE ORDER OF THIS FUNCTION IS THE CONTRACT (spec §3.1, "nothing partial
    // happens"). It used to resolve FIRST and decide afterwards, which made
    // every refusal a side effect: pasting a scene object with target 'assets'
    // still pinned every texture it referenced, pasting a library tile into the
    // editor still imported it while reporting `skipped`, an item refused for
    // one missing mesh still dragged its other assets into the project, and a
    // paste with no scene open imported the lot before finding out. So:
    //
    //   1. decide WHICH ITEMS COULD LAND (target routing only — pure);
    //   2. check the preconditions those items need (a scene, a real parent);
    //   3. PLAN the resolution of exactly what those items reference;
    //   4. drop the items whose assets are missing (unless allowMissing);
    //   5. only now APPLY, restricted to what the survivors need;
    //   6. land them in one undo macro.
    //
    // Steps 1-4 write nothing at all.

    const bool toAssets = options.target == QLatin1String("assets");

    // ---- 1. what could land -----------------------------------------------
    QVector<const ClipItem *> nodeCandidates;
    QVector<const ClipItem *> assetCandidates;
    for (const ClipItem &item : envelope.items) {
        if (item.kind == QLatin1String(clipboardformat::kind::node())) {
            if (toAssets) {
                result.skipped.append({ item.kind,
                    QStringLiteral("scene objects paste in the editor") });
                continue;
            }
            nodeCandidates.append(&item);
        } else if (item.kind == QLatin1String(clipboardformat::kind::asset())) {
            if (!toAssets) {
                result.skipped.append({ item.kind,
                    QStringLiteral("library assets paste in the Assets page") });
                continue;
            }
            assetCandidates.append(&item);
        } else {
            result.skipped.append({ item.kind,
                QStringLiteral("this build has no paste for '%1' items").arg(item.kind) });
        }
    }
    if (nodeCandidates.isEmpty() && assetCandidates.isEmpty()) return result;

    // ---- 2. the preconditions, BEFORE anything is written ------------------
    iris::ScenePtr sc;
    iris::SceneNodePtr parent;
    int index = options.index;
    if (!nodeCandidates.isEmpty()) {
        sc = scene();
        if (!sc || !sceneEdit) {
            result.error = QStringLiteral("no scene is open");
            return result;
        }
        // D7: beside the PRIMARY — its parent, its sibling index + 1 — which is
        // exactly where Duplicate puts a copy. The scene root when nothing is
        // selected, or when an explicit parent was named.
        if (!options.parentGuid.isEmpty()) {
            std::function<iris::SceneNodePtr(const iris::SceneNodePtr &)> find =
                [&](const iris::SceneNodePtr &node) -> iris::SceneNodePtr {
                    if (!node) return iris::SceneNodePtr();
                    if (node->getGUID() == options.parentGuid) return node;
                    for (const auto &child : node->children())
                        if (auto hit = find(child)) return hit;
                    return iris::SceneNodePtr();
                };
            parent = find(sc->getRootNode());
            if (!parent) {
                result.error = QStringLiteral("no node with id '%1'").arg(options.parentGuid);
                return result;
            }
        } else if (selection) {
            if (auto primary = selection->selected()) {
                if (!primary->isRootNode() && !!primary->getParent()) {
                    parent = primary->getParent();
                    if (index < 0) {
                        const int after = primary->siblingIndex();
                        index = after >= 0 ? after + 1 : -1;
                    }
                }
            }
        }
        if (!parent) parent = sc->getRootNode();
    }

    // ---- 3. plan the resolution of what those items reference --------------
    const auto needsOf = [&](const QVector<const ClipItem *> &items) {
        QSet<QString> out;
        for (const ClipItem *item : items)
            for (const QString &guid : neededGuids(*item, envelope)) out.insert(guid);
        return out;
    };
    QSet<QString> candidateNeeds = needsOf(nodeCandidates);
    candidateNeeds.unite(needsOf(assetCandidates));

    ClipboardResolver resolver(db, project);
    const ClipboardResolveReport plan = resolver.plan(envelope, &candidateNeeds);
    result.missing = plan.missing;
    if (!plan.error.isEmpty()) {
        // A payload this build will not accept (a malformed entry) or no
        // library at all: refuse loudly, and still say WHICH asset caused it —
        // an error with no `missing` list leaves the user nothing to act on.
        result.error = plan.error;
        return result;
    }
    QSet<QString> missingGuids;
    for (const auto &missing : plan.missing) missingGuids.insert(missing.guid);

    // ---- 4. drop what cannot be satisfied ---------------------------------
    const auto blocked = [&](const ClipItem &item) {
        if (options.allowMissing) return false;
        for (const QString &guid : neededGuids(item, envelope))
            if (missingGuids.contains(guid)) return true;
        return false;
    };
    QVector<const ClipItem *> nodeItems, assetItems;
    for (const ClipItem *item : nodeCandidates) {
        // REFUSED, not pasted with holes (D5): a node whose mesh guid does not
        // resolve loads with NO mesh and no log — an invisible node the user
        // would report as a bug. `allowMissing` opts into Unreal's behaviour.
        if (blocked(*item)) {
            result.skipped.append({ item->kind,
                QStringLiteral("'%1' needs an asset this library does not have")
                    .arg(item->displayName()) });
            continue;
        }
        nodeItems.append(item);
    }
    for (const ClipItem *item : assetCandidates) {
        if (blocked(*item)) {
            result.skipped.append({ item->kind,
                QStringLiteral("'%1' cannot be imported: its content is not available here")
                    .arg(item->displayName()) });
            continue;
        }
        assetItems.append(item);
    }
    // NOTHING SURVIVED = nothing happens. Not one row, not one pin.
    if (nodeItems.isEmpty() && assetItems.isEmpty()) return result;

    // ---- 5. apply, restricted to what the survivors need -------------------
    QSet<QString> landingNeeds = needsOf(nodeItems);
    landingNeeds.unite(needsOf(assetItems));
    const ClipboardResolveReport applied = resolver.apply(envelope, &landingNeeds);
    result.imported = applied.imported;
    result.pinned = applied.pinned;
    // An asset the PLAN found importable and the apply could not register (a
    // CAS write that failed, a row the database refused) is not "known" and not
    // "missing" — it is a hole, and a node pasted over one carries a dangling
    // guid with no log at all. Both halves are reported and the paste refuses.
    for (const auto &missing : applied.missing) {
        bool seen = false;
        for (const auto &known : result.missing)
            if (known.guid == missing.guid) { seen = true; break; }
        if (!seen) result.missing.append(missing);
    }
    if (!applied.error.isEmpty()) {
        result.error = applied.error;
        if (!result.imported.isEmpty()) emit assetsImported(result.imported);
        return result;
    }

    if (nodeItems.isEmpty()) {                    // an Assets-page paste is done
        if (!result.imported.isEmpty()) emit assetsImported(result.imported);
        return result;
    }

    // ---- 6. the document half, in one undo macro --------------------------
    const bool macro = nodeItems.size() > 1 && undo && undo->stack();
    if (macro) undo->stack()->beginMacro(tr("Paste %1 objects").arg(nodeItems.size()));
    QList<iris::SceneNodePtr> pastedNodes;
    for (const ClipItem *item : nodeItems) {
        SceneFragment fragment;
        fragment.node = item->nodeObject();
        // A CROSS-LIBRARY guid rewrite, when one happened, is applied by the
        // key-aware walk — never the textual replace the archive import path
        // still does (spec §3.2; recorded there as a live contradiction).
        if (!applied.guidMap.isEmpty())
            assetrefs::remapAssetGuids(fragment.node, applied.guidMap);
        fragment.parentGuid = item->parentGuid();
        fragment.siblingIndex = item->siblingIndex();
        // insertFragment mints fresh node guids, applies the Cube -> Cube2
        // naming rule and pushes the same AddSceneNodeCommand every other add
        // uses — the node domain's implementation, unchanged (spec §8).
        if (auto node = sceneEdit->insertFragment(fragment, parent, index)) {
            pastedNodes.append(node);
            result.pasted.append(node->getGUID());
            if (index >= 0) ++index;   // keep the payload's order in the tree
        }
    }
    if (macro) undo->stack()->endMacro();

    if (selection && !pastedNodes.isEmpty()) selection->select(pastedNodes);
    if (!result.imported.isEmpty()) emit assetsImported(result.imported);
    return result;
}

ClipboardCutResult ClipboardService::cutNodes(const QList<iris::SceneNodePtr> &nodes)
{
    ClipboardCutResult result;
    result.copy = copyNodes(nodes);
    if (!result.copy.ok()) {
        result.error = result.copy.error;
        return result;
    }
    // The DELETE is the undoable half and it is the shipped set delete — one
    // macro, the World root and non-removable nodes reported as skipped. The
    // COPY stays on the clipboard either way, so a cut whose delete refused a
    // member is still a copy of everything.
    const auto deleted = sceneEdit->deleteNodes(nodes);
    result.removed = deleted.deleted;
    result.skipped = deleted.skipped;
    return result;
}
