/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "graphdefinition.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "graphbaker.h"
#include "materialhelper.h"
#include "pbrgraphevaluator.h"
#include "pieceemitter.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/materialbundle.h"
#include "../graph/nodegraph.h"
#include "../nodes/test.h"

namespace materials {

namespace {

/// The disposable place a bake's PNGs are written before the store takes
/// them. The store's OWN derived cache, so the baker keeps its
/// `<mapKey>-<hash16>.png` cache hit (a re-save that changes nothing rewrites
/// no pixels) and `assets.gc` can reclaim the whole tree.
/// KNOWN, RECORDED, NOT FIXED IN PHASE 1: `assets.gc` treats `derived/` as a
/// reserved store directory and never sweeps it, so this per-material bake
/// cache is not reclaimed when the material is deleted. It is hash-named and
/// self-limiting (the piece cache has the same shape and the same note), and
/// the whole tree is safe to delete by hand at any time.
QString bakeStagingDir(const QString &materialGuid)
{
    const QString dir = AssetStorePaths::derivedPath("materialbake/" + materialGuid);
    QDir().mkpath(dir);
    return dir;
}

} // namespace

QString bakedMemberRow(Database *db, const QString &materialGuid, const QString &slot,
                       const QString &filePath, QString *errorOut)
{
    const auto fail = [errorOut](const QString &message) {
        if (errorOut) *errorOut = message;
        return QString();
    };
    if (!db || materialGuid.isEmpty() || slot.isEmpty()) return fail(QStringLiteral("no material"));
    if (!QFileInfo::exists(filePath)) return fail(QStringLiteral("the bake wrote no file"));

    const QString name = slot + QStringLiteral(".png");

    // ONE ROW PER SLOT PER MATERIAL, forever. A re-bake moves that row's
    // source pointer instead of minting a second row — the guid a definition
    // (and every scene node wearing a copy of its values) names must not
    // change because the material was saved again.
    QString guid;
    {
        QSqlQuery query(QSqlDatabase::database());
        query.prepare("SELECT guid FROM assets WHERE parent = ? AND name = ? AND type = ?");
        query.addBindValue(materialGuid);
        query.addBindValue(name);
        query.addBindValue(static_cast<int>(ModelTypes::Texture));
        if (query.exec() && query.next()) guid = query.value(0).toString();
    }
    if (guid.isEmpty()) {
        guid = GUIDManager::generateGUID();
        // `parent` = the MATERIAL (M-A, the one relation that is right for a
        // baked map: it is born inside exactly one material and is never
        // shared), which is what makes both browsers hide it already.
        db->createAssetEntry(guid, name, static_cast<int>(ModelTypes::Texture),
                             materialGuid, QString(), QString(), QString(),
                             QByteArray(), QByteArray(), QByteArray(), QByteArray(),
                             AssetViewFilter::AssetsView);
    }

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    QString oid, error;
    if (!AssetCas::ingestFile(conn, root, filePath, guid, QStringLiteral("source"), name,
                              &oid, &error))
        return fail(error.isEmpty() ? QStringLiteral("the store refused the baked map") : error);
    // The bytes changed, so the row's own pointer has to move: ingestFile's
    // INSERT OR IGNORE on (guid, role, name) would leave it on the old oid.
    if (!AssetCas::moveSourcePointer(conn, guid, oid, name, &error))
        return fail(error.isEmpty() ? QStringLiteral("could not publish the baked map") : error);
    AssetCas::writeSidecar(conn, root, guid, nullptr);
    return guid;
}

DefinitionBuild buildDefinition(NodeGraph *graph, const QString &materialGuid,
                                Database *db, Project *project, bool bake)
{
    DefinitionBuild out;
    if (!graph) { out.error = QStringLiteral("no graph"); return out; }
    if (!db)    { out.error = QStringLiteral("no database"); return out; }
    if (materialGuid.isEmpty()) { out.error = QStringLiteral("no material guid"); return out; }

    // WHAT THE BAKER READS: a guid resolves to a path. WHAT WE STORE: the
    // guid. The map from one to the other is the graph's own texture nodes.
    QHash<QString, QString> guidForPath;
    for (auto *node : graph->nodes.values()) {
        if (!node || node->typeName != QLatin1String("texture")) continue;
        auto *texNode = static_cast<TextureNode *>(node);
        const QString guid = texNode->getTextureGuid();
        const QString path = texNode->getTexturePath();
        if (guid.isEmpty() || path.isEmpty()) continue;
        guidForPath.insert(QDir::cleanPath(path), guid);
    }

    // THE EMITTER RUNS FIRST (HLMS_ADOPTION P5): what it takes the baker must
    // not spend time on.
    PieceEmitter::Result emitted;
    PieceEmitter::emitAndStore(graph, MaterialHelper::textureResolver(), &emitted);

    GraphBaker::Options opts;
    opts.resolution = graph->settings.bakeResolution;
    opts.bakeMaps = bake;
    opts.emittedSockets = emitted.emittedSockets;
    if (bake) {
        opts.outputDir = bakeStagingDir(materialGuid);
        // One behaviour everywhere: the baker's reported value NAMES the file
        // it wrote (see GraphApi::bake). This function reads them back to
        // ingest them, so a bare name would work here by accident and mislead
        // the next reader.
        opts.relativePrefix = opts.outputDir + QLatin1Char('/');
    }
    const GraphBaker::Result baked = GraphBaker::run(graph, opts,
                                                     MaterialHelper::textureResolver());

    QJsonObject values = baked.eval.values;

    // (2) EVERY MAP VALUE BECOMES A GUID.
    QJsonObject bakeMaps;
    for (auto it = baked.maps.constBegin(); it != baked.maps.constEnd(); ++it) {
        // A BAKED map: its pixels are ours, so they become a member row.
        const QString file = QDir(opts.outputDir).filePath(QFileInfo(it.value().toString()).fileName());
        QString error;
        const QString memberGuid = bakedMemberRow(db, materialGuid, it.key(), file, &error);
        if (memberGuid.isEmpty()) {
            // A slot we cannot store is a slot the material does not carry —
            // never a path left behind for another machine to fail on.
            values.remove(it.key());
            out.unsupportedNodes.append(QStringLiteral("%1 <- bake (%2)").arg(it.key(), error));
            continue;
        }
        values[it.key()] = memberGuid;
        bakeMaps[it.key()] = memberGuid;
        out.bakedMembers.append(memberGuid);
    }
    for (auto it = baked.passthrough.constBegin(); it != baked.passthrough.constEnd(); ++it) {
        // A PASSTHROUGH map: the user's own image, bound directly. It is
        // already an asset — name it.
        const QString guid = guidForPath.value(QDir::cleanPath(it.value().toString()));
        if (guid.isEmpty()) { values.remove(it.key()); continue; }
        values[it.key()] = guid;
    }
    // Anything still holding a path after both passes names a file this build
    // cannot account for; the definition writer would refuse the whole save
    // for it, so the slot is dropped with a reason instead.
    for (const QString &slot : MaterialBundle::textureSlots()) {
        if (!values.contains(slot)) continue;
        const QString value = values.value(slot).toString();
        if (!MaterialBundle::looksLikePath(value)) continue;
        values.remove(slot);
        out.unsupportedNodes.append(QStringLiteral("%1 <- an unstored image").arg(slot));
    }

    // (3) The COLOUR SPELLING is not normalised here: `MaterialBundle::write`
    // does it for every writer, beside the path guard. It was here first and
    // that was wrong by this lane's own argument — `MaterialsApi::create`
    // builds a definition without going through this function and shipped the
    // evaluator's object-valued colours straight into the store, so a
    // scripted graph material rendered BLACK in the commit that claimed the
    // defect fixed. One place, and it is the one every definition passes.

    // THE GRAPH IS THE TRUTH FOR EVERYTHING A GRAPH CAN SAY, and exactly
    // THREE rows are not (PRESET-UNIFY-1, tightened in its fix round).
    //
    // The three: `roughnessLowerBound` / `roughnessUpperBound` — the remap
    // that turns a specular map into roughness, which nine of the shipped
    // presets rely on — and `normalFactor`. No master socket, no setting and
    // no node expresses any of them, so a save that dropped them would change
    // a material the user had not touched.
    //
    // NOTHING ELSE IS CARRIED, and the first draft of this block carrying
    // more was a defect in four ways: a glass material set back to Opaque
    // stayed glass for ever; a material once saved Translucent could never be
    // made opaque; disconnecting the Alpha socket left the old alpha; and
    // deleting a UV tiling node brought the LAST SAVED tiling back instead of
    // identity. The alpha rows are the graph's because every alphaMode the
    // material has is a blend mode now (see BlendMode), and the UV rows are
    // the graph's whenever the graph has a texture node at all — an identity
    // fold then means identity, and absence in a definition is the material's
    // own default, which is what erasing a row looks like here. A graph with
    // NO texture node cannot have folded a UV transform at all, so for that
    // one shape the previous transform is kept rather than silently reset.
    {
        static const QStringList kNoGraphCanSay = {
            QStringLiteral("roughnessLowerBound"),
            QStringLiteral("roughnessUpperBound"),
            QStringLiteral("normalFactor"),
        };
        static const QStringList kUvRows = {
            QStringLiteral("textureScale"),   QStringLiteral("textureScaleV"),
            QStringLiteral("textureOffsetU"), QStringLiteral("textureOffsetV"),
            QStringLiteral("textureRotation"),
        };
        bool hasTextureNode = false;
        for (auto *node : graph->nodes.values())
            if (node && node->typeName == QLatin1String("texture")) { hasTextureNode = true; break; }

        // AND THE ROWS THAT BELONG TO ONE BLEND MODE (fix round 2). A graph
        // can NAME Refractive but has no socket for what refraction is made
        // of, so a Refractive material saved from its graph came back at the
        // constructor's 0.35 strength whatever the user had set. They are the
        // same class as the roughness bounds — rows no graph can say — for
        // exactly the mode that uses them, and for any other mode they are
        // not carried at all, so switching OUT of Refractive drops them.
        static const QStringList kRefractiveRows = {
            QStringLiteral("refractionStrength"), QStringLiteral("ior"),
            QStringLiteral("fresnelColor"),       QStringLiteral("separateFresnel"),
            QStringLiteral("useFresnelColor"),
        };
        QStringList carry = kNoGraphCanSay;
        if (!hasTextureNode) carry += kUvRows;
        if (graph->settings.blendMode == BlendMode::Refractive) carry += kRefractiveRows;

        const QJsonObject previous =
            MaterialBundle::read(db, materialGuid, project)
                .value(QStringLiteral("values")).toObject();
        for (const QString &key : carry) {
            if (values.contains(key) || !previous.contains(key)) continue;
            values[key] = previous.value(key);
        }
    }

    QJsonObject definition;
    definition[QStringLiteral("materialType")] = QStringLiteral("pbr");
    definition[QStringLiteral("name")] = graph->settings.name;
    definition[QStringLiteral("values")] = values;
    definition[QStringLiteral("shadergraph")] = graph->serialize();

    QJsonObject bakeRecord;
    bakeRecord[QStringLiteral("maps")] = bakeMaps;
    bakeRecord[QStringLiteral("animated")] = baked.eval.animated || emitted.animated;
    if (emitted.accepted)
        bakeRecord[QStringLiteral("emittedSockets")] =
            QJsonArray::fromStringList(emitted.emittedSockets);
    definition[QStringLiteral("bake")] = bakeRecord;

    out.definition = definition;
    out.unsupportedNodes += baked.eval.unsupportedNodes;
    return out;
}

} // namespace materials
