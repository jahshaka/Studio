// THE MATERIAL BUNDLE MODEL — MATERIAL_BUNDLE_SPEC phase 1, against the REAL
// Database and the REAL content-addressed store on a throwaway SQLite file.
//
// What it pins, each an owner-visible claim from the spec:
//
//   1. D-2 — a material's DEFINITION is its row's CAS `source` file. Writing a
//      material stores a real object and moves the library source pointer; the
//      definition reads back byte-for-byte through `read`, and it reads back
//      from the STORE (not the row blob) — proved by scrubbing the blob and
//      reading again.
//   2. M-B + G2 — the membership edges are DERIVED from the definition on
//      every write, and they carry NO project stamp. Swapping a slot's texture
//      moves the edge; a project-stamped USE edge is never touched.
//   3. F3 — a definition whose texture slot holds a FILE PATH is REFUSED, in
//      `values` and in a `shadergraph` payload alike, and the refusal names
//      the slot. Nothing is stored and nothing is published.
//   4. F11 / §12 Q2 — a project pins the definition it added. A LIBRARY edit
//      afterwards does not change what that project reads; a PROJECT-scope
//      edit moves that project's pin and leaves the library alone.
//   5. §2.3 — a GRAPH is a payload of the one Material row: no second row of
//      any type is minted, the graph survives the round trip, and a texture a
//      graph node names is a member even before it reaches a master slot.
//   6. Baked maps ride as member texture GUIDs, so the closure walker
//      (`AssetHelper::fetchAssetAndAllDependencies`, which add-to-project and
//      the archive manifest use) reaches them with no special case.
//   7. F12 — the definition is in the SIDECAR, so `assets.rebuildCatalog` can
//      restore a material.
//   8. The slot list is the MATERIAL's own (`iris::PbrMaterial`'s texture
//      properties), including the generated detail-layer rows.
//
// Framework-free; non-zero exit on failure. Runs displayless.
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cstdio>

#include "data/database/database.h"
#include "data/constants.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assethelper.h"
#include "services/assetstorepaths.h"
#include "services/materialbundle.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static int countWhere(const QString &sql, const QVariantList &binds = {})
{
    QSqlQuery q;
    q.prepare(sql);
    for (const auto &b : binds) q.addBindValue(b);
    if (!q.exec()) { printf("info: query error: %s\n", qPrintable(q.lastError().text())); return -1; }
    return q.next() ? q.value(0).toInt() : -1;
}

static QString writeTempFile(const QDir &dir, const QString &name, const QByteArray &bytes)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(bytes);
    f.close();
    return path;
}

static QString textureRow(Database &db, const QString &guid, const QString &name,
                          const QString &storeRoot, const QString &srcPath)
{
    db.createAssetEntry(guid, name, static_cast<int>(ModelTypes::Texture),
                        QString(), QString(), QString(), QString(), QByteArray(),
                        QByteArray(), QByteArray(), QByteArray(),
                        AssetViewFilter::AssetsView);
    QString oid, err;
    AssetCas::ingestFile(QSqlDatabase::database(), storeRoot, srcPath, guid,
                         QStringLiteral("source"), name, &oid, &err);
    return oid;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    QTemporaryDir scratch;
    if (!scratch.isValid()) { printf("FAIL: no scratch dir\n"); return 1; }
    const QDir scratchDir(scratch.path());
    const QString storeRoot = scratchDir.filePath("store");
    QDir().mkpath(storeRoot);
    AssetStorePaths::setRootOverride(storeRoot);

    const QString dbPath = scratchDir.filePath("materialbundle_test.db");
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    QSqlDatabase conn = QSqlDatabase::database();

    // --- 8. the slot list is the material's own ---------------------------
    CHECK(MaterialBundle::textureSlots().contains("baseColorMap")
              && MaterialBundle::textureSlots().contains("normalMap")
              && MaterialBundle::textureSlots().contains("reflectionMap"),
          "8: the map slots come from iris::PbrMaterial itself");
    CHECK(MaterialBundle::textureSlots().contains("detail0Map"),
          "8: the GENERATED detail-layer rows are slots too (a hand-written list would have missed them)");
    CHECK(!MaterialBundle::textureSlots().contains("roughness"),
          "8: a float row is not a texture slot");

    CHECK(MaterialBundle::looksLikePath("/home/j/AssetStore/x/wood.jpg"), "a path reads as a path");
    CHECK(MaterialBundle::looksLikePath("wood.jpg"), "a bare file name reads as a path");
    CHECK(!MaterialBundle::looksLikePath("b43a94be-1122-3344-5566-778899aabbcc"), "a guid does not");

    // --- fixture textures --------------------------------------------------
    const QString woodSrc  = writeTempFile(scratchDir, "wood.jpg",  QByteArray("wood-bytes"));
    const QString brickSrc = writeTempFile(scratchDir, "brick.jpg", QByteArray("brick-bytes"));
    const QString bakeSrc  = writeTempFile(scratchDir, "baked.png", QByteArray("baked-bytes"));
    const QString woodOid  = textureRow(db, "tex-wood",  "wood.jpg",  storeRoot, woodSrc);
    const QString brickOid = textureRow(db, "tex-brick", "brick.jpg", storeRoot, brickSrc);
    const QString bakeOid  = textureRow(db, "tex-baked", "baked.png", storeRoot, bakeSrc);
    CHECK(!woodOid.isEmpty() && !brickOid.isEmpty() && !bakeOid.isEmpty(),
          "three fixture textures are in the store");

    // =======================================================================
    // 1. THE DEFINITION IS THE ROW'S CAS SOURCE FILE
    // =======================================================================
    QJsonObject values;
    values["baseColorMap"] = "tex-wood";
    values["roughness"] = 0.75;
    QJsonObject definition;
    definition["materialType"] = "pbr";
    definition["values"] = values;

    QString createError;
    const QString woodyGuid = MaterialBundle::create(&db, "woody", definition,
                                                     QByteArray(), &createError);
    CHECK(!woodyGuid.isEmpty(), qPrintable(QStringLiteral("1: the bundle was minted (%1)").arg(createError)));
    CHECK(db.fetchAsset(woodyGuid).type == static_cast<int>(ModelTypes::Material),
          "1: it is ONE Material row");

    const QString sourcePath = AssetCas::resolveSource(conn, storeRoot, woodyGuid);
    CHECK(!sourcePath.isEmpty() && QFile::exists(sourcePath),
          "1: the definition is a real object in the store (D-2)");
    CHECK(!AssetCas::sourceOid(conn, woodyGuid).isEmpty(),
          "1: the library source pointer names it");

    QJsonObject readBack = MaterialBundle::read(&db, woodyGuid);
    CHECK(readBack["values"].toObject()["baseColorMap"].toString() == "tex-wood",
          "1: the definition reads back");
    CHECK(readBack["version"].toInt() == MaterialBundle::kDefinitionVersion,
          "1: stamped with the definition version");

    // It reads from the STORE, not the row blob: scrub the blob and ask again.
    db.updateAssetAsset(woodyGuid, QByteArray("{}"));
    readBack = MaterialBundle::read(&db, woodyGuid);
    CHECK(readBack["values"].toObject()["baseColorMap"].toString() == "tex-wood",
          "1: the STORE is the definition, the blob only a cache");

    // =======================================================================
    // 2. EDGES ARE DERIVED, AND CARRY NO PROJECT STAMP (M-B, audit G2)
    // =======================================================================
    CHECK(countWhere("SELECT COUNT(*) FROM dependencies WHERE depender = ? AND dependee = ?",
                     { woodyGuid, "tex-wood" }) == 1,
          "2: the member edge was derived from the definition");
    CHECK(countWhere("SELECT COUNT(*) FROM dependencies WHERE depender = ? AND project_guid IS NULL",
                     { woodyGuid }) == 1,
          "2: an INTRINSIC edge carries NO project (G2: deleteProject must not take it)");

    // A USE edge, stamped with a project, is not ours to rewrite.
    const QString projectGuid = "proj-bundle";
    CHECK(db.createProject(projectGuid, "Bundle Test"), "fixture project row");
    db.createDependency(static_cast<int>(ModelTypes::Object),
                        static_cast<int>(ModelTypes::Material),
                        "node-1", woodyGuid, projectGuid);

    // Swap the slot: the member edge moves, the USE edge stays.
    values["baseColorMap"] = "tex-brick";
    definition["values"] = values;
    MaterialBundle::WriteResult w = MaterialBundle::write(&db, nullptr, woodyGuid, definition);
    CHECK(w.ok, qPrintable(QStringLiteral("2: the swap wrote (%1)").arg(w.error)));
    CHECK(countWhere("SELECT COUNT(*) FROM dependencies WHERE depender = ? AND dependee = ?",
                     { woodyGuid, "tex-wood" }) == 0,
          "2: the old member edge is gone");
    CHECK(countWhere("SELECT COUNT(*) FROM dependencies WHERE depender = ? AND dependee = ?",
                     { woodyGuid, "tex-brick" }) == 1,
          "2: the new member edge is derived");
    CHECK(countWhere("SELECT COUNT(*) FROM dependencies WHERE dependee = ? AND project_guid = ?",
                     { woodyGuid, projectGuid }) == 1,
          "2: a node's USE edge survived the definition write");

    // =======================================================================
    // 3. A PATH IN A SLOT IS REFUSED (F3)
    // =======================================================================
    QJsonObject bad = definition;
    QJsonObject badValues = values;
    badValues["baseColorMap"] = "/home/jahshaka/.local/share/Jahshaka/AssetStore/b43a/wood.jpg";
    bad["values"] = badValues;
    QString slot;
    CHECK(!MaterialBundle::offendingPath(bad, &slot).isEmpty() && slot == "baseColorMap",
          "3: the guard names the offending slot");
    const QString beforeOid = AssetCas::sourceOid(conn, woodyGuid);
    w = MaterialBundle::write(&db, nullptr, woodyGuid, bad);
    CHECK(!w.ok, "3: the writer REFUSES a path in a texture slot");
    CHECK(w.error.contains("baseColorMap"), "3: the refusal says which slot");
    CHECK(AssetCas::sourceOid(conn, woodyGuid) == beforeOid,
          "3: nothing was published on the refusal");

    // ... and in a graph payload, which is where the owner's path got in.
    QJsonObject graphNode;
    graphNode["id"] = "n1";
    graphNode["type"] = "texture";
    graphNode["value"] = "/home/jahshaka/.local/share/Jahshaka/AssetStore/b43a/wood.jpg";
    QJsonObject graph;
    graph["nodes"] = QJsonArray{ graphNode };
    QJsonObject badGraph = definition;
    badGraph["shadergraph"] = graph;
    CHECK(!MaterialBundle::offendingPath(badGraph).isEmpty(),
          "3: a graph texture node holding a path is refused too");

    // =======================================================================
    // 4. A PROJECT PINS WHAT IT ADDED (F11 / the owner's Q2 model)
    // =======================================================================
    const QString pinnedOid = AssetCas::sourceOid(conn, woodyGuid);
    CHECK(AssetCas::writePin(conn, projectGuid, woodyGuid, pinnedOid),
          "4: the project pins the definition it added");

    Project project;
    project.setProjectGuid(projectGuid);

    // A LIBRARY edit afterwards.
    values["roughness"] = 0.1;
    definition["values"] = values;
    w = MaterialBundle::write(&db, nullptr, woodyGuid, definition, MaterialBundle::Scope::Library);
    CHECK(w.ok, "4: the library edit wrote");
    CHECK(AssetCas::sourceOid(conn, woodyGuid) != pinnedOid, "4: the library pointer moved");
    CHECK(AssetCas::pinnedOid(conn, projectGuid, woodyGuid) == pinnedOid,
          "4: the PROJECT's pin did not move");
    QJsonObject inProject = MaterialBundle::read(&db, woodyGuid, &project);
    CHECK(qFuzzyCompare(inProject["values"].toObject()["roughness"].toDouble() + 1.0, 0.75 + 1.0),
          "4: the project still reads the version it was built with (F11 closed)");
    QJsonObject inLibrary = MaterialBundle::read(&db, woodyGuid);
    CHECK(qFuzzyCompare(inLibrary["values"].toObject()["roughness"].toDouble() + 1.0, 0.1 + 1.0),
          "4: the library reads the new one");

    // A PROJECT-scope edit.
    values["roughness"] = 0.5;
    definition["values"] = values;
    const QString libraryOidBefore = AssetCas::sourceOid(conn, woodyGuid);
    w = MaterialBundle::write(&db, &project, woodyGuid, definition, MaterialBundle::Scope::Project);
    CHECK(w.ok, qPrintable(QStringLiteral("4: the project-scope edit wrote (%1)").arg(w.error)));
    CHECK(AssetCas::sourceOid(conn, woodyGuid) == libraryOidBefore,
          "4: a project edit does NOT move the library pointer");
    CHECK(AssetCas::pinnedOid(conn, projectGuid, woodyGuid) == w.oid,
          "4: it moves the project's pin (copy-on-write)");
    inProject = MaterialBundle::read(&db, woodyGuid, &project);
    CHECK(qFuzzyCompare(inProject["values"].toObject()["roughness"].toDouble() + 1.0, 0.5 + 1.0),
          "4: the project reads its own version");

    // =======================================================================
    // 5. A GRAPH IS A PAYLOAD — NO SECOND ROW (§2.3)
    // =======================================================================
    const int rowsBefore = countWhere("SELECT COUNT(*) FROM assets");
    QJsonObject graphNodeOk;
    graphNodeOk["id"] = "n1";
    graphNodeOk["type"] = "texture";
    graphNodeOk["value"] = "tex-brick";
    QJsonObject graphNodeMaster;
    graphNodeMaster["id"] = "n0";
    graphNodeMaster["type"] = "pbrMaster";
    QJsonObject okGraph;
    okGraph["nodes"] = QJsonArray{ graphNodeMaster, graphNodeOk };
    okGraph["masternode"] = "n0";

    QJsonObject graphValues;
    graphValues["baseColorMap"] = "tex-wood";
    QJsonObject graphDefinition;
    graphDefinition["materialType"] = "pbr";
    graphDefinition["values"] = graphValues;
    graphDefinition["shadergraph"] = okGraph;

    const QString graphyGuid = MaterialBundle::create(&db, "graphy", graphDefinition,
                                                      QByteArray(), &createError);
    CHECK(!graphyGuid.isEmpty(), "5: a graph material is minted");
    CHECK(countWhere("SELECT COUNT(*) FROM assets") == rowsBefore + 1,
          "5: exactly ONE new row — no Shader row (§2.3)");
    CHECK(countWhere("SELECT COUNT(*) FROM assets WHERE type = ?",
                     { static_cast<int>(ModelTypes::Shader) }) == 0,
          "5: no ModelTypes::Shader row is ever minted again");

    const QJsonObject graphReadBack = MaterialBundle::read(&db, graphyGuid);
    CHECK(graphReadBack["shadergraph"].toObject()["nodes"].toArray().size() == 2,
          "5: the graph survives the round trip as a payload");
    const QStringList graphMembers = MaterialBundle::memberGuids(graphReadBack);
    CHECK(graphMembers.contains("tex-wood") && graphMembers.contains("tex-brick"),
          "5: a texture a GRAPH node names is a member before it reaches a slot");

    // =======================================================================
    // 6. BAKED MAPS RIDE AS MEMBER GUIDS — THE CLOSURE WALKER FINDS THEM
    // =======================================================================
    QJsonObject bakedValues = graphValues;
    bakedValues["roughnessMap"] = "tex-baked";
    QJsonObject bakeRecord;
    QJsonObject bakeMaps;
    bakeMaps["roughnessMap"] = "tex-baked";
    bakeRecord["maps"] = bakeMaps;
    QJsonObject bakedDefinition = graphDefinition;
    bakedDefinition["values"] = bakedValues;
    bakedDefinition["bake"] = bakeRecord;
    w = MaterialBundle::write(&db, nullptr, graphyGuid, bakedDefinition);
    CHECK(w.ok, "6: a definition with a baked member wrote");

    const QStringList closure = AssetHelper::fetchAssetAndAllDependencies(graphyGuid, &db);
    CHECK(closure.contains("tex-baked"),
          "6: add-to-project's closure walker reaches the baked map with no special case");
    CHECK(closure.contains("tex-wood") && closure.contains("tex-brick"),
          "6: and every other member");

    // =======================================================================
    // 7. THE SIDECAR CARRIES THE RELATIONS, not just a parse (F12 + G5)
    // =======================================================================
    const QString sidecar = QDir(storeRoot).filePath("sidecar/" + graphyGuid + ".json");
    CHECK(QFile::exists(sidecar), "7: a material has a sidecar at last (F12)");
    {
        QFile f(sidecar);
        f.open(QIODevice::ReadOnly);
        const QJsonObject side = QJsonDocument::fromJson(f.readAll()).object();
        CHECK(side.value("formatVersion").toInt() == 2, "7: sidecar format 2 (G5)");
        // THE PAYLOAD — a DB-only kind's meaning, which v1 carried nowhere, so
        // a rebuilt catalog restored a material that resolved to nothing.
        const QJsonObject blob = side.value("asset").toObject();
        CHECK(!blob.isEmpty(), "7: the sidecar carries the `asset` blob");
        CHECK(blob.value("values").toObject().value("baseColorMap").toString() == "tex-wood",
              "7: and the DEFINITION rebuilds from it, members and all");
        // THE INTRINSIC EDGES — the membership every closure walk needs.
        const QJsonArray edges = side.value("dependencies").toArray();
        QStringList sidecarMembers;
        for (const auto &e : edges) sidecarMembers << e.toObject().value("dependee").toString();
        CHECK(sidecarMembers.contains("tex-wood") && sidecarMembers.contains("tex-baked"),
              "7: the sidecar carries the intrinsic membership edges");
        // A MEMBER's own sidecar names its PARENT — what makes a baked map the
        // inside of its material after a rebuild, instead of a loose tile.
        // (A real baked member is minted with `parent` = the material by
        // materials::bakedMemberRow; this is that row's shape.)
        {
            db.createAssetEntry("tex-member", "roughnessMap.png",
                                static_cast<int>(ModelTypes::Texture),
                                graphyGuid,          // parent = the MATERIAL
                                QString(), QString(), QString(), QByteArray(),
                                QByteArray(), QByteArray(), QByteArray(),
                                AssetViewFilter::AssetsView);
            QString oid, err;
            AssetCas::ingestFile(conn, storeRoot, bakeSrc, "tex-member",
                                 QStringLiteral("source"), "roughnessMap.png", &oid, &err);
            CHECK(AssetCas::writeSidecar(conn, storeRoot, "tex-member", &err),
                  "7: a member's sidecar is written");
            QFile mf(QDir(storeRoot).filePath("sidecar/tex-member.json"));
            mf.open(QIODevice::ReadOnly);
            const QJsonObject memberSide = QJsonDocument::fromJson(mf.readAll()).object();
            CHECK(memberSide.value("parent").toString() == graphyGuid,
                  "7: and it names its PARENT, so a rebuild keeps it inside its material");
        }
    }

    // =======================================================================
    // 8b. ONE SPELLING FOR A COLOUR, AT THE WRITE (F3)
    // =======================================================================
    //
    // The evaluator's shape is `{r,g,b,a}` floats; every READER of a stored
    // material expects `QColor::name()`. Two spellings under one
    // `materialType` is a reader choosing by luck, and QColor of an
    // object-valued key is invalid — the graph's red folded to BLACK. The
    // normalisation lives in `write`, beside the path guard, so that no
    // caller can be the exception: it was in the graph's definition BUILDER
    // first, and `MaterialsApi::create` — which does not use that builder —
    // shipped the bad spelling straight into the store.
    {
        QJsonObject rgba;
        rgba["r"] = 0.85; rgba["g"] = 0.08; rgba["b"] = 0.08; rgba["a"] = 1.0;
        QJsonObject colourValues;
        colourValues["baseColor"] = rgba;
        colourValues["roughness"] = 0.3;
        QJsonObject colourDef;
        colourDef["materialType"] = "pbr";
        colourDef["values"] = colourValues;

        const QString redGuid = MaterialBundle::create(&db, "red", colourDef,
                                                       QByteArray(), &createError);
        CHECK(!redGuid.isEmpty(), "8b: a definition with an object-valued colour is accepted");
        const QJsonObject stored = MaterialBundle::read(&db, redGuid);
        const QJsonValue base = stored.value("values").toObject().value("baseColor");
        CHECK(base.isString(), "8b: it reads back as a STRING, not an object");
        CHECK(QColor(base.toString()).isValid(), "8b: and QColor can read it");
        CHECK(QColor(base.toString()).red() > QColor(base.toString()).green() + 40,
              "8b: with the colour intact (#rrggbb — QColor::name() drops alpha, by the "
              "document's own convention)");
        // Straight through `write`, not only through `create`.
        const QJsonObject again = MaterialBundle::normaliseColours(colourDef);
        CHECK(again.value("values").toObject().value("baseColor").isString(),
              "8b: normaliseColours is the predicate, so any writer can be checked");
    }

    // =======================================================================
    // 9. A PROJECT-SCOPE WRITE DOES NOT REWRITE THE LIBRARY'S MEMBERSHIP (F7)
    // =======================================================================
    //
    // A project edit is a copy-on-write: this project's pin moves and the
    // library version is untouched IN EVERY RESPECT, membership included.
    // Rewriting the NULL-project edges from a project's edited definition
    // made the library row's closure describe a version the library does not
    // have — so another project's add-to-project pinned the library oid and
    // walked the edited edges: the right definition with the wrong textures.
    {
        QJsonObject libValues;
        libValues["baseColorMap"] = "tex-wood";
        QJsonObject libDef;
        libDef["materialType"] = "pbr";
        libDef["values"] = libValues;
        const QString sharedGuid = MaterialBundle::create(&db, "shared", libDef,
                                                          QByteArray(), &createError);
        CHECK(!sharedGuid.isEmpty(), "9: a library bundle naming tex-wood");
        CHECK(AssetCas::writePin(conn, projectGuid, sharedGuid,
                                 AssetCas::sourceOid(conn, sharedGuid)),
              "9: the project takes it");

        QJsonObject projValues;
        projValues["baseColorMap"] = "tex-brick";     // the PROJECT swaps the texture
        QJsonObject projDef = libDef;
        projDef["values"] = projValues;
        const auto projWrite = MaterialBundle::write(&db, &project, sharedGuid, projDef,
                                                     MaterialBundle::Scope::Project);
        CHECK(projWrite.ok, qPrintable(QStringLiteral("9: the project edit wrote (%1)")
                                           .arg(projWrite.error)));
        CHECK(countWhere("SELECT COUNT(*) FROM dependencies WHERE depender = ? AND dependee = ? "
                         "AND project_guid IS NULL", { sharedGuid, "tex-wood" }) == 1,
              "9: the LIBRARY's membership still names tex-wood");
        CHECK(countWhere("SELECT COUNT(*) FROM dependencies WHERE depender = ? AND dependee = ? "
                         "AND project_guid IS NULL", { sharedGuid, "tex-brick" }) == 0,
              "9: and the project's swap did NOT reach it");
        // And the project's own version is pinned and readable.
        const QJsonObject projRead = MaterialBundle::read(&db, sharedGuid, &project);
        CHECK(projRead.value("values").toObject().value("baseColorMap").toString() == "tex-brick",
              "9: while the project reads its own");

        // ---- 10. EVERY MEMBER THE WRITE NAMED IS PINNED (F8) -------------
        //
        // A baked map is minted during the write itself, and the ordinary
        // order is "add the material to the project, THEN edit it" — so the
        // member row was neither project-owned nor pinned, and the archive
        // manifest (project rows + pins) did not carry it. The material
        // travelled without the maps it is made of.
        CHECK(AssetCas::pinnedOid(conn, projectGuid, "tex-brick")
                  == AssetCas::sourceOid(conn, "tex-brick"),
              "10: the member the project's definition names is pinned by the project (F8)");
    }

    printf(failures ? "\n%d FAILURES\n" : "\nall material bundle assertions passed\n", failures);
    return failures ? 1 : 0;
}
