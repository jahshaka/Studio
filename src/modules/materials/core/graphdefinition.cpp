/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "graphdefinition.h"

#include <QColor>
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
    if (bake) opts.outputDir = bakeStagingDir(materialGuid);
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

    // (3) ONE ENCODING IN A STORED DEFINITION, and it is the DOCUMENT's.
    //
    // The evaluator's colours are `{r,g,b,a}` floats — its own working shape,
    // handed straight to `PbrGraphEvaluator::materialFromValues` on the live
    // preview and apply paths. But the definition is read by
    // `MaterialReader::parsePbrMaterial`, which is what reads EVERY other
    // material in the app (an image material's, a preset's, a node's copied
    // values in the scene blob) and which spells a colour the way
    // `SceneWriter::writeSceneNodeMaterial` writes it: `QColor::name()`.
    //
    // Two spellings of one slot under one `materialType: "pbr"` is a reader
    // choosing by luck — and it showed as a BLACK material thumbnail, because
    // `QColor(QString())` from an object-valued key is invalid. The
    // evaluation shape stays inside the evaluator; what is STORED is the
    // document's.
    for (const QString &key : values.keys()) {
        const QJsonValue value = values.value(key);
        if (!value.isObject()) continue;
        const QJsonObject rgba = value.toObject();
        if (!rgba.contains(QStringLiteral("r"))) continue;
        const QColor colour = QColor::fromRgbF(
            qBound(0.0, rgba.value(QStringLiteral("r")).toDouble(), 1.0),
            qBound(0.0, rgba.value(QStringLiteral("g")).toDouble(), 1.0),
            qBound(0.0, rgba.value(QStringLiteral("b")).toDouble(), 1.0),
            qBound(0.0, rgba.value(QStringLiteral("a")).toDouble(1.0), 1.0));
        values[key] = colour.name();
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
