/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// archive.closure — THE CLOSURE WITHOUT N+1 (D11-LIBRARY-SCALE §3.6), on the REAL
// Database over a throwaway SQLite file: 12 models -> 2 materials + a mesh each,
// every material -> 3 of 60 shared textures, every row carrying a thumbnail and a
// definition BLOB. services/assetclosure.cpp's walk (expand) and its payload
// (describe, with the row BLOBs) run with the query log on:
//
//   * the walk costs ONE statement per BFS LEVEL (it cost one per asset);
//   * the payload costs ONE header statement and ONE edge statement for the whole
//     set, ONE files statement per asset (CasContentSource: the pin rides it as a
//     join) and ONE BLOB read per asset — never a full row, never a thumbnail;
//   * so the statements per asset are <= 1 + the BLOB reads (= the assets), and
//   * the answer is IDENTICAL to the query-per-node walk it replaced: the same
//     guids in the same order, and every described row equal to fetchAsset's
//     fields and fetchAssetGUIDAndDependencies' edges.
// Headless (offscreen); never touches the user's JahLibrary.db.

#include <QApplication>
#include <QFile>
#include <QSet>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QUuid>
#include <cstdio>

#include "data/database/database.h"
#include "data/project.h"
#include "export/exportcontentsource.h"
#include "services/assetcas.h"
#include "services/assetclosure.h"

static int failures = 0;
#define CHECK(cond, ...)                                                                   \
    do {                                                                                   \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); }  \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
        std::fflush(stdout);                                                               \
    } while (0)

static QString newGuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

// THE WALK IT REPLACED, verbatim in shape: a dependency query per node.
static QStringList referenceWalk(const QStringList &seeds, Database &db)
{
    QStringList out, frontier;
    QSet<QString> seen;
    const auto push = [&](const QString &guid) {
        if (guid.isEmpty() || seen.contains(guid)) return;
        seen.insert(guid);
        out.append(guid);
        frontier.append(guid);
    };
    for (const QString &s : seeds) push(s);
    while (!frontier.isEmpty()) {
        const QString guid = frontier.takeFirst();
        for (const QString &dep : db.fetchAssetGUIDAndDependencies(guid)) push(dep);
    }
    return out;
}

static int statementsNamed(const QString &name)
{
    int n = 0;
    for (const Database::QueryLogEntry &e : Database::queryLogEntries())
        if (e.name == name) n += e.count;
    return n;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    const QString dbPath = QStringLiteral("archive_closure_test.db");
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    const QByteArray thumb(4096, 'T');   // a BLOB the closure must never read
    const auto make = [&](ModelTypes type, const QString &name) {
        const QString g = newGuid();
        db.createAssetEntry(g, name, static_cast<int>(type), QString(), QString(), QString(), QString(), thumb,
                            QByteArray("{\"p\":\"") + name.toUtf8() + "\"}", QByteArray(),
                            QByteArray("{\"a\":\"") + name.toUtf8() + "\"}", AssetViewFilter::AssetsView);
        return g;
    };
    QStringList textures, materials, models;
    for (int i = 0; i < 60; ++i) textures << make(ModelTypes::Texture, QStringLiteral("tex_%1.png").arg(i));
    for (int i = 0; i < 24; ++i) {
        const QString m = make(ModelTypes::Material, QStringLiteral("mat_%1.material").arg(i));
        for (int t = 0; t < 3; ++t)
            db.createDependency(int(ModelTypes::Material), int(ModelTypes::Texture), m, textures.at((i * 7 + t * 11) % 60));
        materials << m;
    }
    for (int i = 0; i < 12; ++i) {
        const QString o = make(ModelTypes::Object, QStringLiteral("model_%1.glb").arg(i));
        const QString mesh = make(ModelTypes::Mesh, QStringLiteral("model_%1.mesh").arg(i));
        db.createDependency(int(ModelTypes::Object), int(ModelTypes::Mesh), o, mesh);
        db.createDependency(int(ModelTypes::Object), int(ModelTypes::Material), o, materials.at(i * 2));
        db.createDependency(int(ModelTypes::Object), int(ModelTypes::Material), o, materials.at(i * 2 + 1));
        models << o;
    }

    // ---- THE WALK: one statement per level --------------------------------
    const QStringList reference = referenceWalk(models, db);
    Database::setQueryLog(true);
    const QStringList closure = assetclosure::expand(models, &db);
    const int walkStatements = Database::queryLogStatements();
    CHECK(closure == reference, "the batched walk answers the per-node walk's guids in its order (%d assets)",
          int(closure.size()));
    CHECK(statementsNamed(QStringLiteral("fetchDependencyEdges")) == 3 && walkStatements == 3,
          "the walk over %d assets ran %d statements: one per BFS level (3), not one per asset",
          int(closure.size()), walkStatements);

    // ---- THE PAYLOAD: two set statements, one files + one BLOB read per asset --
    QTemporaryDir store;
    assetclosure::Options options;
    options.storeRoot = store.path();
    options.includeRowBlobs = true;
    options.inlineLimitBytes = 0;
    Database::setQueryLog(true);
    const int files0 = CasContentSource::statements();
    const auto described = assetclosure::describe(closure, &db, options);
    const int assets = int(closure.size());
    const int dbStatements = Database::queryLogStatements();
    const int fileStatements = CasContentSource::statements() - files0;
    const int blobReads = statementsNamed(QStringLiteral("fetchAssetRowBlobs"));
    std::printf("   describe: %d assets, %d catalog statements (%d BLOB reads), %d files statements\n", assets,
                dbStatements, blobReads, fileStatements);
    CHECK(described.size() == assets, "every closure asset is described");
    CHECK(statementsNamed(QStringLiteral("fetchAssetHeaders")) == 1, "ONE header statement for the whole set");
    CHECK(statementsNamed(QStringLiteral("fetchDependencyEdges")) == 1, "ONE edge statement for the whole set");
    CHECK(blobReads == assets, "the BLOB reads = the assets (%d)", blobReads);
    CHECK(fileStatements == assets, "ONE files statement per asset — the pin rides it (%d)", fileStatements);
    CHECK(statementsNamed(QStringLiteral("fetchAsset")) == 0, "no full-row read (thumbnail included) of any asset");
    bool anyThumb = false;
    for (const Database::QueryLogEntry &e : Database::queryLogEntries()) anyThumb |= e.selectsThumbnail;
    CHECK(!anyThumb, "no statement of the payload selects a thumbnail");
    const int perAssetStatements = dbStatements + fileStatements - blobReads;
    CHECK(perAssetStatements <= assets + 2,
          "statements per asset <= 1 + the BLOB reads: %d non-BLOB statements for %d assets",
          perAssetStatements, assets);
    Database::setQueryLog(false);

    // ---- THE SAME ANSWER as the per-row reads -----------------------------
    bool same = true;
    for (const QString &guid : closure) {
        const AssetRecord row = db.fetchAsset(guid);
        const auto it = described.constFind(guid);
        if (it == described.constEnd()) { same = false; continue; }
        same &= it->name == row.name && it->typeId == row.type && it->parent == row.parent
                && it->viewFilter == row.view_filter && it->blob == row.asset && it->properties == row.properties
                && it->dependencies == db.fetchAssetGUIDAndDependencies(guid, false);
    }
    CHECK(same, "every described row equals fetchAsset's fields and the per-node edges");

    db.closeDatabase();
    QFile::remove(dbPath);
    std::printf(failures ? "archive.closure: %d FAILURES\n" : "archive.closure: all checks passed\n", failures);
    return failures ? 1 : 0;
}
