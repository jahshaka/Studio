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
#include "irisgl/core/guid.h"
#include "services/assettags.h"
#include "services/materialbundle.h"
#include "services/materialmembers.h"
#include "services/assetdelete.h"
#include "services/assetgc.h"
#include "services/assetmigration.h"
#include "data/database/casschema.h"
#include "commands/resetmaterialcommand.h"
#include "services/projectassets.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include <QSqlDriver>
#include <sqlite3.h>
#include <cstring>

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
                        QString(), assethome::assets(), assethome::Origin::Import, QString(), QString(), QByteArray(),
                        QByteArray(), QByteArray(), QByteArray());
    QString oid, err;
    AssetCas::ingestFile(db.connection(), storeRoot, srcPath, guid,
                         QStringLiteral("source"), name, &oid, &err);
    return oid;
}

// THE TEST's OWN FAULT INJECTION (case 14): an SQLite authorizer on the library's handle. mode 1: every read of
// sqlite_master denied (the table probes); mode 2: reads of `projects` denied INSIDE A SELECT (a pin count), its
// DELETE untouched; mode 3: sqlite_master's reads denied from the GC's 7th statement on (its pin-table probe:
// tableExists x3, COUNT x2, the mapped oids, then the probe).
// mode 4: every read of `project_assets` INSIDE A SELECT denied ("is it pinned here", the pin's oid), its writes
// untouched; mode 5: only the pin's OID (`project_assets.oid_pin`) inside a SELECT ("is it pinned" still reads).
struct Deny { int mode = 0; int selects = 0; bool inSelect = false; int probesDenied = 0; };
static Deny gDeny;
static int denyReads(void *, int action, const char *table, const char *column, const char *, const char *)
{
    if (action == SQLITE_SELECT) { gDeny.inSelect = true; ++gDeny.selects; return SQLITE_OK; }
    if (action == SQLITE_INSERT || action == SQLITE_UPDATE || action == SQLITE_DELETE) gDeny.inSelect = false;
    if (action != SQLITE_READ || !table) return SQLITE_OK;
    const bool master = std::strcmp(table, "sqlite_master") == 0 || std::strcmp(table, "sqlite_schema") == 0;
    const bool pins = std::strcmp(table, "project_assets") == 0;
    if (gDeny.mode == 1 && master) return SQLITE_DENY;
    if (gDeny.mode == 2 && gDeny.inSelect && std::strcmp(table, "projects") == 0) return SQLITE_DENY;
    if (gDeny.mode == 3 && master && gDeny.selects >= 7) {
        if (gDeny.selects == 7) ++gDeny.probesDenied;   // the pin-table probe's OWN denials (not the dead-pin probes')
        return SQLITE_DENY;
    }
    if (gDeny.mode == 4 && gDeny.inSelect && pins) return SQLITE_DENY;
    if (gDeny.mode == 5 && gDeny.inSelect && pins && column && std::strcmp(column, "oid_pin") == 0) return SQLITE_DENY;
    if (gDeny.mode == 6 && gDeny.inSelect && std::strcmp(table, "asset_files") == 0) return SQLITE_DENY;
    return SQLITE_OK;
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

    db::Connection &conn = db.connection();

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
    const QString woodyGuid = MaterialBundle::create(&db, "woody", definition, assethome::assets(),
                                                     QByteArray(), &createError, assethome::Origin::ExplicitSave);
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

    const QString graphyGuid = MaterialBundle::create(&db, "graphy", graphDefinition, assethome::assets(),
                                                      QByteArray(), &createError, assethome::Origin::ExplicitSave);
    CHECK(!graphyGuid.isEmpty(), "5: a graph material is minted");
    CHECK(countWhere("SELECT COUNT(*) FROM assets") == rowsBefore + 1,
          "5: exactly ONE new row — no Shader row (§2.3)");

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
        CHECK(side.value("formatVersion").toInt() == 3 && side.value("home").toString() == "assets",
              "7: sidecar format 3 (G5 + ASSETS-HOME-1: the home is recorded)");
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
                                graphyGuid,          assethome::assets(), assethome::Origin::Import, QString(), QString(), QByteArray(),
                                QByteArray(), QByteArray(), QByteArray());
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

        const QString redGuid = MaterialBundle::create(&db, "red", colourDef, assethome::assets(),
                                                       QByteArray(), &createError, assethome::Origin::ExplicitSave);
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
        const QString sharedGuid = MaterialBundle::create(&db, "shared", libDef, assethome::assets(),
                                                          QByteArray(), &createError, assethome::Origin::ExplicitSave);
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

    // =======================================================================
    // 11. A SHIPPED PRESET IS READ-ONLY IN FACT (phase 3, owner §12 Q2)
    // =======================================================================
    //
    // "Defaults are read-only SAMPLES… to edit you CREATE a material or CLONE
    // a default." A drawer that offers no edit gesture is not enough: the
    // Materials page AUTOSAVES every 1.5 s, so a preset opened by any route
    // at all would have been republished under the guid the whole app treats
    // as immutable — the tray tile, the drag payload and every saved scene
    // name it. The refusal is in the WRITER, it names the preset, and the one
    // door past it (`writeShipped`, the seeder's) is a different function.
    {
        const QString presetGuid = QStringLiteral("00000000-0000-0000-0000-000000002024");
        CHECK(MaterialBundle::shippedPresetName(presetGuid) == QLatin1String("Gold PBR"),
              "11: a reserved guid answers the preset's NAME");
        CHECK(MaterialBundle::shippedPresetName(QStringLiteral("not-a-preset")).isEmpty(),
              "11: an ordinary guid is not a preset");

        db.createAssetEntry(presetGuid, "Gold PBR", static_cast<int>(ModelTypes::Material),
                            QString(), assethome::assets(), assethome::Origin::Import, QString(), QString(), QByteArray(),
                            QByteArray(), QByteArray(), QByteArray());
        QJsonObject presetValues;
        presetValues["baseColor"] = "#ffd700";
        QJsonObject presetDef;
        presetDef["materialType"] = "pbr";
        presetDef["values"] = presetValues;

        const auto refused = MaterialBundle::write(&db, nullptr, presetGuid, presetDef);
        CHECK(!refused.ok, "11: the ONE writer refuses a shipped preset");
        CHECK(refused.error.contains(QLatin1String("Gold PBR")),
              qPrintable(QStringLiteral("11: ...and names it (%1)").arg(refused.error)));
        CHECK(AssetCas::sourceOid(conn, presetGuid).isEmpty(),
              "11: nothing was published by the refused write");

        const auto seeded = MaterialBundle::writeShipped(&db, presetGuid, presetDef);
        CHECK(seeded.ok, qPrintable(QStringLiteral("11: the seeder's door publishes (%1)")
                                        .arg(seeded.error)));
        CHECK(!AssetCas::sourceOid(conn, presetGuid).isEmpty(),
              "11: ...and the definition is a store object like any other material's");
        CHECK(MaterialBundle::read(&db, presetGuid).value("values").toObject()
                  .value("baseColor").toString() == QLatin1String("#ffd700"),
              "11: ...and reads back");

        // A second ordinary write, now that the row HAS a definition, is
        // still refused: read-only is a property of the guid, not of the
        // row's state.
        presetValues["baseColor"] = "#000000";
        presetDef["values"] = presetValues;
        CHECK(!MaterialBundle::write(&db, nullptr, presetGuid, presetDef).ok,
              "11: a seeded preset is still refused (the guid is what is read-only)");
        CHECK(MaterialBundle::read(&db, presetGuid).value("values").toObject()
                  .value("baseColor").toString() == QLatin1String("#ffd700"),
              "11: ...and the stored definition did not move");
    }

    // ---- 12. THE NAME LAWS, AT THE ONE NAME WRITER -----------------------
    //
    // `assettags::write` is what every rename in the app goes through — the
    // verb, the Assets page's Update button and, since PRESET-UNIFY-1's
    // second fix round, the Materials page's two rename doors, which used to
    // call `Database::renameAsset` directly and so knew neither law.
    //
    // Two laws, and the second one is the one that was missing: a shipped
    // preset cannot be renamed, and NOTHING ELSE MAY TAKE a shipped preset's
    // name — because a preset is reached BY NAME (material.apply,
    // materials.loadGraph, the drag payload), so a user's asset called
    // "Gold PBR" is not a duplicate label, it is an asset nothing can ever
    // address by name while the name goes on resolving to the preset.
    {
        const QString goldGuid = Constants::Reserved::DefaultMaterials.key("Gold PBR");
        const QString mine = QString::fromStdString(iris::newGuid());
        db.createAssetEntry(mine, QStringLiteral("My Material"),
                            static_cast<int>(ModelTypes::Material), QString(), assethome::assets(), assethome::Origin::Import,
                            QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                            QByteArray());

        CHECK(assettags::rename(&db, mine, QStringLiteral("Something Else")),
              "12: an ordinary material renames");
        CHECK(db.fetchAsset(mine).name == QLatin1String("Something Else"),
              "12: ...and the row took it");

        CHECK(!assettags::rename(&db, mine, QStringLiteral("Gold PBR")),
              "12: nothing may take a shipped preset's NAME");
        CHECK(!assettags::rename(&db, mine, QStringLiteral("gold pbr")),
              "12: ...case-insensitively, which is how a preset is matched");
        CHECK(db.fetchAsset(mine).name == QLatin1String("Something Else"),
              "12: ...and the refused rename changed nothing");

        db.createAssetEntry(goldGuid, QStringLiteral("Gold PBR"),
                            static_cast<int>(ModelTypes::Material), QString(), assethome::assets(), assethome::Origin::Import,
                            QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                            QByteArray());
        CHECK(!assettags::rename(&db, goldGuid, QStringLiteral("Fred")),
              "12: and the preset itself cannot be renamed");
        CHECK(db.fetchAsset(goldGuid).name == QLatin1String("Gold PBR"),
              "12: ...one guid, one name");
    }

    // =======================================================================
    // 13. A FAILED READ NEVER DECIDES A DELETE (NOQT-DATABASE-1 stage 2, the spec's law): where "who uses it"
    //     (Database::hasMultipleDependers) or "who pins it" (assetdelete::livePins / countAssetPins) decides a
    //     delete, a read that FAILS is never "nobody": the member SURVIVES and the delete is refused by name.
    //     The failure is injected by a TEMP VIEW that shadows the table on the library's connection (SQLite
    //     resolves an unqualified name to the temp schema first) and raises "integer overflow" on its rows.
    // =======================================================================
    {
        const auto row = [&db](const QString &guid, ModelTypes type, const QString &parent) {
            db.createAssetEntry(guid, guid, static_cast<int>(type), parent, assethome::materials(),
                                assethome::Origin::Create, QString(), QString(), QByteArray(), QByteArray(),
                                QByteArray(), QByteArray());
        };
        const auto alive = [&db](const QString &guid) { return !db.fetchAsset(guid).guid.isEmpty(); };
        const auto listedUnused = [&db](const QString &guid) {
            for (const materialmembers::Unused &u : materialmembers::unused(&db, nullptr, QStringLiteral("gone-material")))
                if (u.guid == guid) return true;
            return false;
        };
        const auto shadow = [&conn](const char *table) {
            db::Error e;
            conn.exec(std::string("CREATE TEMP VIEW ") + table + " AS SELECT * FROM main." + table
                          + " WHERE abs(-9223372036854775807 - 1) IS NOT NULL", &e);
            return !e;
        };
        const auto unshadow = [&conn](const char *table) {
            db::Error e;
            conn.exec(std::string("DROP VIEW temp.") + table, &e);
        };
        row(QStringLiteral("gone-material"), ModelTypes::Material, QString());

        // CONTROL: a baked map of the material that nothing uses or pins IS removed (the clean deletes).
        row(QStringLiteral("gone-orphan"), ModelTypes::Texture, QStringLiteral("gone-material"));
        QString err;
        const QVector<materialmembers::Unused> cleaned = materialmembers::cleanUnused(&db, nullptr, QStringLiteral("gone-material"), &err);
        CHECK(cleaned.size() == 1 && !alive(QStringLiteral("gone-orphan")),
              QStringLiteral("13: CONTROL: an unused baked map is removed (%1 removed)").arg(cleaned.size()).toUtf8().constData());

        // (a) WHO USES IT, unread: a map another material uses, its dependencies read failing.
        row(QStringLiteral("gone-used"), ModelTypes::Texture, QStringLiteral("gone-material"));
        row(QStringLiteral("gone-user"), ModelTypes::Material, QString());
        db.createDependency(static_cast<int>(ModelTypes::Material), static_cast<int>(ModelTypes::Texture),
                            QStringLiteral("gone-user"), QStringLiteral("gone-used"));
        CHECK(shadow("dependencies"), "13a: the dependencies read is made to fail");
        CHECK(!listedUnused(QStringLiteral("gone-used")),
              "13a: a map whose users could not be read is NOT listed unused (the DECISION, not an accident of the write)");
        materialmembers::cleanUnused(&db, nullptr, QStringLiteral("gone-material"), &err);
        unshadow("dependencies");
        CHECK(alive(QStringLiteral("gone-used")),
              "13a: a map whose users could not be read SURVIVES the clean (never 'nobody uses it')");

        // (b) WHO PINS IT, unread: a map a live project pins, its pins read failing.
        row(QStringLiteral("gone-pinned"), ModelTypes::Texture, QStringLiteral("gone-material"));
        db.createProject(QStringLiteral("gone-project"), QStringLiteral("Gone Project"));
        AssetCas::writePin(conn, QStringLiteral("gone-project"), QStringLiteral("gone-pinned"), QString());
        db.removeDependenciesByType(QStringLiteral("gone-user"), ModelTypes::Texture);   // (a)'s user lets go
        CHECK(shadow("project_assets"), "13b: the pins read is made to fail");
        CHECK(!listedUnused(QStringLiteral("gone-pinned")),
              "13b: a map whose pins could not be read is NOT listed unused (the DECISION, not an accident of the write)");
        materialmembers::cleanUnused(&db, nullptr, QStringLiteral("gone-material"), &err);
        unshadow("project_assets");
        CHECK(alive(QStringLiteral("gone-pinned")),
              "13b: a map whose pins could not be read SURVIVES the clean (never 'no project holds it')");
        const assetdelete::Outcome direct = [&]() {
            shadow("project_assets");
            const assetdelete::Outcome out = assetdelete::remove(&db, QStringLiteral("gone-pinned"));
            unshadow("project_assets");
            return out;
        }();
        CHECK(alive(QStringLiteral("gone-pinned")) && !direct.ok && direct.error.contains(QStringLiteral("could not be read")),
              QStringLiteral("13b: assetdelete::remove with its pin count unread is REFUSED by name: '%1'")
                  .arg(direct.error).toUtf8().constData());
    }

    // =======================================================================
    // 14. THE AUDIT's FINDINGS 1-5 (stage 2): a FAILED READ never decides a delete. THE INJECTION, test-side only:
    //     an SQLite AUTHORIZER the test installs on the library's own handle (the Qt driver's, which the layer
    //     borrows until stage 3) DENIES a table's reads at prepare - the statement fails by name, the product
    //     unchanged; a persistent TRIGGER that RAISEs on one row's insert; a library that is CLOSED.
    // =======================================================================
    {
        sqlite3 *raw = *static_cast<sqlite3 *const *>(QSqlDatabase::database().driver()->handle().constData());
        const auto alive = [&db](const QString &guid) { return !db.fetchAsset(guid).guid.isEmpty(); };
        const auto libraryRow = [&db](const QString &guid, bool listed) {
            db.createAssetEntry(guid, guid, static_cast<int>(ModelTypes::Texture), QString(), assethome::materials(),
                                assethome::Origin::Create, QString(), QString(), QByteArray(), QByteArray(),
                                QByteArray(), QByteArray());
            if (!listed) db.setAssetListed(guid, false);
        };
        const auto project = [&db](const QString &guid) { db.createProject(guid, guid); };
        const auto pin = [&conn](const QString &projectGuid, const QString &guid) {
            AssetCas::writePin(conn, projectGuid, guid, QString());
        };

        // (2) THE TABLE PROBE's failure (sqlite_master's read denied) is no answer either.
        libraryRow(QStringLiteral("f2-pinned"), true);
        project(QStringLiteral("f2-project"));
        pin(QStringLiteral("f2-project"), QStringLiteral("f2-pinned"));
        {
            bool read = true;
            const int count = (gDeny = Deny{ 1 }, sqlite3_set_authorizer(raw, denyReads, nullptr),
                               db.countAssetPins(QStringLiteral("f2-pinned"), &read));
            sqlite3_set_authorizer(raw, nullptr, nullptr);
            CHECK(!read, QStringLiteral("14.2: a failed table probe answers NO READ (count %1, read %2)").arg(count).arg(read)
                             .toUtf8().constData());
        }

        // (3) THE ROW's OWN VETO (deleteAssetRow) and THE PROJECT's ORPHAN REAP (deleteProject): a failed pin count
        //     (the projects table's reads denied inside a SELECT; its own DELETE still runs) keeps the row.
        libraryRow(QStringLiteral("f3-pinned"), true);
        project(QStringLiteral("f3-holder"));
        pin(QStringLiteral("f3-holder"), QStringLiteral("f3-pinned"));
        gDeny = Deny{ 2 };
        sqlite3_set_authorizer(raw, denyReads, nullptr);
        db.deleteAsset(QStringLiteral("f3-pinned"));
        sqlite3_set_authorizer(raw, nullptr, nullptr);
        CHECK(alive(QStringLiteral("f3-pinned")), "14.3: a pinned asset whose pin count could not be read SURVIVES deleteAsset");
        libraryRow(QStringLiteral("f3-orphan"), false);
        project(QStringLiteral("f3-dying"));
        pin(QStringLiteral("f3-dying"), QStringLiteral("f3-orphan"));
        pin(QStringLiteral("f3-holder"), QStringLiteral("f3-orphan"));
        gDeny = Deny{ 2 };
        sqlite3_set_authorizer(raw, denyReads, nullptr);
        db.deleteProject(QStringLiteral("f3-dying"));
        sqlite3_set_authorizer(raw, nullptr, nullptr);
        CHECK(alive(QStringLiteral("f3-orphan")),
              "14.3: an unlisted row another project pins SURVIVES the reap when its pin count could not be read");

        // (4) removeFromProject: the same refusal as remove.
        libraryRow(QStringLiteral("f4-shared"), false);
        project(QStringLiteral("f4-p"));
        project(QStringLiteral("f4-q"));
        pin(QStringLiteral("f4-p"), QStringLiteral("f4-shared"));
        pin(QStringLiteral("f4-q"), QStringLiteral("f4-shared"));
        CHECK(alive(QStringLiteral("f4-shared")) && db.countAssetPins(QStringLiteral("f4-shared")) == 2,
              "14.4: CONTROL: the shared row exists, pinned by two projects");
        gDeny = Deny{ 2 };
        sqlite3_set_authorizer(raw, denyReads, nullptr);
        const assetdelete::Outcome dropped = assetdelete::removeFromProject(&db, QStringLiteral("f4-shared"), QStringLiteral("f4-p"));
        sqlite3_set_authorizer(raw, nullptr, nullptr);
        CHECK(alive(QStringLiteral("f4-shared")) && db.isAssetPinnedBy(QStringLiteral("f4-p"), QStringLiteral("f4-shared"))
                  && dropped.error.contains(QStringLiteral("could not be read")),
              QStringLiteral("14.4: removeFromProject with a read failing is REFUSED by name before its first unpin: '%1'")
                  .arg(dropped.error).toUtf8().constData());

        // (5) THE GC's PIN-TABLE PROBE: a pin-only object is never "unreferenced" because the probe failed.
        {
            db::Error e;
            conn.exec("INSERT INTO files (oid, size, ext, refcount) VALUES ('f5pinonly', 3, 'png', 0)", &e);
            pin(QStringLiteral("f3-holder"), QStringLiteral("f2-pinned"));
            conn.exec("UPDATE project_assets SET oid_pin = 'f5pinonly' WHERE project_guid = 'f3-holder' AND asset_guid = 'f2-pinned'", &e);
            gDeny = Deny{ 3 };
            sqlite3_set_authorizer(raw, denyReads, nullptr);
            const AssetGc::Report gc = AssetGc::collect(conn, storeRoot);
            sqlite3_set_authorizer(raw, nullptr, nullptr);
            bool listed = false;
            for (const AssetGc::Item &item : gc.unreferencedObjects.items) listed = listed || item.id == QLatin1String("f5pinonly");
            CHECK(gDeny.probesDenied > 0, QStringLiteral("14.5: CONTROL: the pin-table probe was denied (%1; %2 selects; gc '%3')")
                                               .arg(gDeny.probesDenied).arg(gDeny.selects).arg(gc.error)
                                               .toUtf8().constData());
            CHECK(!gc.ok && gc.error.contains(QStringLiteral("a read of the catalog failed")) && !listed,
                  QStringLiteral("14.5: a failed pin-table probe REFUSES the sweep by name, the pin-only object never listed "
                                 "(ok %1, '%2', listed %3)").arg(gc.ok).arg(gc.error).arg(listed).toUtf8().constData());
        }

        // (1) THE REBUILD COUNTS ITS FAILED ROWS: one sidecar's insert RAISEs (a trigger on a fresh catalog).
        {
            const QString rebuildRoot = scratchDir.filePath("rebuild_store");
            QDir().mkpath(QDir(rebuildRoot).filePath("sidecar"));
            for (const char *guid : { "r1-good", "r1-poisoned" }) {
                QJsonObject sidecar;
                sidecar["formatVersion"] = 3;
                sidecar["guid"] = QString::fromLatin1(guid);
                sidecar["name"] = QString::fromLatin1(guid);
                sidecar["type"] = static_cast<int>(ModelTypes::Material);
                sidecar["home"] = assethome::kindName(assethome::Kind::MaterialsLibrary);
                sidecar["origin"] = QStringLiteral("create");
                QFile f(QDir(rebuildRoot).filePath(QStringLiteral("sidecar/%1.json").arg(QString::fromLatin1(guid))));
                f.open(QIODevice::WriteOnly);
                f.write(QJsonDocument(sidecar).toJson());
            }
            const QString rebuildDb = scratchDir.filePath("rebuild_target.db");
            {
                db::Options o;
                o.name = "rebuild-setup";
                db::Error e;
                db::Connection setup = db::Connection::open(rebuildDb.toStdString(), o, &e);
                setup.exec(CasSchema::kAssetsTableForRebuild, &e);
                setup.exec("CREATE TRIGGER poison BEFORE INSERT ON assets WHEN NEW.guid = 'r1-poisoned' "
                           "BEGIN SELECT RAISE(ABORT, 'injected: this row is refused'); END", &e);
            }
            const AssetMigration::RebuildReport rebuilt = AssetMigration::rebuildCatalog(rebuildDb, rebuildRoot);
            CHECK(rebuilt.assets == 1, QStringLiteral("14.1: CONTROL: the good row was written before the rollback (%1)").arg(rebuilt.assets)
                                           .toUtf8().constData());
            CHECK(!rebuilt.ok, QStringLiteral("14.1: a rebuild with a FAILED row is not ok (ok %1, error '%2')")
                                   .arg(rebuilt.ok).arg(rebuilt.error).toUtf8().constData());
            CHECK(rebuilt.failedRows == 1 && rebuilt.failedGuids == QStringList{ QStringLiteral("r1-poisoned") }
                      && rebuilt.error.contains(QStringLiteral("r1-poisoned")) && !rebuilt.committed,
                  QStringLiteral("14.1: ...the failed row is COUNTED and NAMED (%1: %2)").arg(rebuilt.failedRows)
                      .arg(rebuilt.failedGuids.join(QLatin1Char(','))).toUtf8().constData());
            // 17.7 (the confirm's F7): A REBUILD WITH A FAILED ROW COMMITS NOTHING AND STAMPS NOTHING - the catalog as it
            //      was before it (no row, the generation unread-as-written: 0), so a later boot reads it again.
            {
                db::Options o;
                o.name = "rebuild-after";
                db::Error e;
                db::Connection after = db::Connection::open(rebuildDb.toStdString(), o, &e);
                db::Statement rows(after, "SELECT count(*) FROM assets");
                const int left = rows.step(&e) ? int(rows.int64(0)) : -1;
                db::Statement version(after, "PRAGMA user_version");
                const int stamped = version.step(&e) ? int(version.int64(0)) : -1;
                CHECK(left == 0 && stamped == 0,
                      QStringLiteral("17.7: a rebuild with a failed row leaves the catalog as it was: rows %1 (0), generation %2 (0)")
                          .arg(left).arg(stamped).toUtf8().constData());
            }
            // 17.7h (the sweep's h): ensureCasSchema whose generation READ fails stamps nothing (the read of
            //       `PRAGMA user_version` denied by the authorizer, its write not).
            {
                db::Error e;
                conn.exec("PRAGMA user_version = 3", &e);
                const auto generation = [&conn]() {
                    db::Statement s(conn, "PRAGMA user_version");
                    db::Error read;
                    return s.step(&read) ? int(s.int64(0)) : -1;
                };
                sqlite3_set_authorizer(raw, [](void *, int action, const char *pragma, const char *value, const char *, const char *) {
                    return action == SQLITE_PRAGMA && pragma && std::strcmp(pragma, "user_version") == 0 && !value
                               ? SQLITE_DENY : SQLITE_OK;
                }, nullptr);
                AssetCas::ensureCasSchema(conn);
                sqlite3_set_authorizer(raw, nullptr, nullptr);
                const int after = generation();
                AssetCas::ensureCasSchema(conn);
                const int stampedAfter = generation();
                CHECK(after == 3 && stampedAfter == CasSchema::kUserVersion,
                      QStringLiteral("17.7h: a generation that could not be read is not stamped (%1, was 3); CONTROL: read, "
                                     "it is (%2)").arg(after).arg(stampedAfter).toUtf8().constData());
            }
        }
        // 15. THE LEAD's THREE MORE SITES (the reader's deepening). 15a: the material's BAKED-MAP REAP
        //     (materialmembers::reapExclusiveMembers) never deletes a map whose USERS could not be read - a TEMP VIEW
        //     shadowing `dependencies` raises only on the user's own edge, which the users read (by dependee)
        //     reaches and the delete's own edge read (by depender) does not.
        {
            db::Error e;
            libraryRow(QStringLiteral("m15"), true);
            libraryRow(QStringLiteral("c15"), true);
            libraryRow(QStringLiteral("u15"), true);
            conn.exec("UPDATE assets SET parent = 'm15' WHERE guid = 'c15'", &e);
            conn.exec("INSERT INTO dependencies (depender_type, dependee_type, project_guid, depender, dependee, id) "
                      "VALUES (1, 2, NULL, 'u15', 'c15', 'e15')", &e);
            materialmembers::reapExclusiveMembers(&db, QStringLiteral("m15"));
            CHECK(alive(QStringLiteral("c15")), "15a: CONTROL: a baked map another asset uses survives the reap");
            conn.exec("CREATE TEMP VIEW dependencies AS SELECT * FROM main.dependencies "
                      "WHERE CASE WHEN depender = 'u15' THEN abs(-9223372036854775807 - 1) ELSE 1 END", &e);
            bool usersRead = true;
            db.hasMultipleDependers(QStringLiteral("c15"), &usersRead);
            CHECK(!usersRead, "15a: CONTROL: the map's users read is made to fail");
            materialmembers::reapExclusiveMembers(&db, QStringLiteral("m15"));
            conn.exec("DROP VIEW temp.dependencies", &e);
            CHECK(alive(QStringLiteral("c15")), "15a: a baked map whose users could not be read SURVIVES the reap");
        }
        // 15b: THE FOREIGN-COPY REFUSAL (MaterialBundle::foreignCopyRefusal) never lets a project take another
        //      project's preset copy because who holds it could not be read (the projects reads denied).
        {
            db::Error e;
            libraryRow(QStringLiteral("f15copy"), true);
            conn.exec("UPDATE assets SET properties = CAST('{\"presetMaster\": \"00000000-0000-0000-0000-000000002024\"}' "
                      "AS BLOB) WHERE guid = 'f15copy'", &e);
            project(QStringLiteral("f15-p"));
            pin(QStringLiteral("f15-p"), QStringLiteral("f15copy"));
            const QString held = MaterialBundle::foreignCopyRefusal(&db, QStringLiteral("f15copy"), QStringLiteral("f15-q"));
            CHECK(!held.isEmpty(), QStringLiteral("15b: CONTROL: another project's copy is refused: '%1'").arg(held)
                                       .toUtf8().constData());
            gDeny = Deny{ 2 };
            sqlite3_set_authorizer(raw, denyReads, nullptr);
            const QString unread = MaterialBundle::foreignCopyRefusal(&db, QStringLiteral("f15copy"), QStringLiteral("f15-q"));
            sqlite3_set_authorizer(raw, nullptr, nullptr);
            CHECK(unread.contains(QStringLiteral("could not be read")),
                  QStringLiteral("15b: a copy whose holders could not be read is REFUSED by name: '%1'").arg(unread)
                      .toUtf8().constData());
        }

        // 16. AN UNREADABLE SIDECAR IS NEVER "NO ASSET" (the lead's ruling (A)): a forced sweep over a store holding a
        //     sidecar that is not JSON (the catalog has no row for it, as after a rebuild that could not read it)
        //     KEEPS that sidecar and every unreferenced or stray object - its own cannot be told apart - and names it.
        //     The control: a good orphan IS swept by an earlier sweep of the same store.
        {
            const QString gcRoot = scratchDir.filePath("gc16_store");
            QDir().mkpath(QDir(gcRoot).filePath("sidecar"));
            db::Options o;
            o.name = "gc16";
            db::Error e;
            db::Connection gc = db::Connection::open(scratchDir.filePath("gc16.db").toStdString(), o, &e);
            gc.exec(CasSchema::kAssetsTableForRebuild, &e);
            AssetCas::ensureCasSchema(gc);
            const auto object = [&gcRoot](const QString &oid) {
                const QString path = AssetStorePaths::objectPathIn(gcRoot, oid, QStringLiteral("png"));
                QDir().mkpath(QFileInfo(path).absolutePath());
                QFile f(path);
                f.open(QIODevice::WriteOnly);
                f.write("bytes");
                return path;
            };
            const QString orphan = object(QStringLiteral("aa16000000000000000000000000000000000000"));
            const AssetGc::Report first = AssetGc::sweep(gc, gcRoot, /*dryRun*/ false, /*force*/ true);
            CHECK(first.ok && !QFile::exists(orphan),
                  QStringLiteral("16: CONTROL: a good orphan IS swept (ok %1, '%2')").arg(first.ok).arg(first.error)
                      .toUtf8().constData());
            const QString kept = object(QStringLiteral("bb16000000000000000000000000000000000000"));
            const QString sidecarPath = QDir(gcRoot).filePath("sidecar/k16.json");
            {
                QFile f(sidecarPath);
                f.open(QIODevice::WriteOnly);
                f.write("this sidecar is not JSON {");
            }
            const AssetGc::Report second = AssetGc::sweep(gc, gcRoot, /*dryRun*/ false, /*force*/ true);
            CHECK(QFile::exists(kept) && QFile::exists(sidecarPath),
                  QStringLiteral("16: an object of an UNREADABLE sidecar, and the sidecar, SURVIVE a forced sweep (object %1, "
                                 "sidecar %2)").arg(QFile::exists(kept)).arg(QFile::exists(sidecarPath)).toUtf8().constData());
            CHECK(second.ok && second.unreadableSidecars.items.size() == 1
                      && second.unreadableSidecars.items.first().id == QLatin1String("k16"),
                  QStringLiteral("16: ...and the sweep NAMES it (%1 unreadable: '%2')")
                      .arg(second.unreadableSidecars.items.size())
                      .arg(second.unreadableSidecars.items.isEmpty() ? QString()
                                                                    : second.unreadableSidecars.items.first().reason)
                      .toUtf8().constData());
        }

        // 17. THE CONFIRM's FINDINGS (stage 2's fix round, builder B3): a FAILED PIN READ never moves a project's own
        //     version (F1, F2), and a sidecar that parses but that the rebuild cannot place is never "no asset" (F6).
        //     The injection as in 14: mode 4 denies every read of `project_assets` inside a SELECT, mode 5 only its
        //     `oid_pin` (so "is it pinned here" still reads and the pin's OID does not).
        {
            const QDir paintedDir(scratchDir.filePath(QStringLiteral("painted")));
            QDir().mkpath(paintedDir.path());
            const auto texture = [&](const QString &guid) {
                const QString name = guid + QStringLiteral(".png");
                return textureRow(db, guid, name, storeRoot,
                                  writeTempFile(scratchDir, name, (guid + QStringLiteral(" library")).toUtf8()));
            };
            // the project's OWN version (copy-on-write): new bytes under the same file name, only its pin moves.
            const auto painted = [&](const QString &guid, Project &owner) {
                QString err;
                return ProjectAssets::copyOnWrite(
                    guid, writeTempFile(paintedDir, guid + QStringLiteral(".png"), (guid + QStringLiteral(" painted")).toUtf8()),
                    &db, &owner, &err);
            };
            const auto pinOf = [&conn](const QString &projectGuid, const QString &guid) {
                return AssetCas::pinnedOid(conn, projectGuid, guid);
            };
            const auto material = [&](const QString &guid, const QString &member) {
                QJsonObject values;
                values[QStringLiteral("baseColorMap")] = member;
                QJsonObject def;
                def[QStringLiteral("materialType")] = QStringLiteral("pbr");
                def[QStringLiteral("values")] = values;
                QString err;
                return MaterialBundle::create(&db, guid, def, assethome::assets(), QByteArray(), &err,
                                              assethome::Origin::ExplicitSave);
            };

            // 17.1 (F1) ResetMaterialCommand's redo after an undo: "is it pinned here" unread is no "no".
            {
                texture(QStringLiteral("t17r"));
                const QString library = AssetCas::sourceOid(conn, QStringLiteral("t17r"));
                db.createProject(QStringLiteral("p17r"), QStringLiteral("p17r"));
                Project owner;
                owner.setProjectGuid(QStringLiteral("p17r"));
                AssetCas::writePin(conn, QStringLiteral("p17r"), QStringLiteral("t17r"), library);   // the default's pin
                ResetMaterialCommand reset(&db, &owner, iris::MeshNode::create(), iris::MaterialPtr(),
                                           { QStringLiteral("t17r") }, { QStringLiteral("t17r") });
                reset.undo();
                CHECK(!db.isAssetPinnedBy(QStringLiteral("p17r"), QStringLiteral("t17r")),
                      "17.1: CONTROL: the undo took the default's pin back");
                const QString own = painted(QStringLiteral("t17r"), owner);
                CHECK(!own.isEmpty() && own != library && pinOf(QStringLiteral("p17r"), QStringLiteral("t17r")) == own,
                      "17.1: CONTROL: the project then holds its OWN version of the texture");
                gDeny = Deny{ 4 };
                sqlite3_set_authorizer(raw, denyReads, nullptr);
                reset.redo();
                sqlite3_set_authorizer(raw, nullptr, nullptr);
                const QString after = pinOf(QStringLiteral("p17r"), QStringLiteral("t17r"));
                CHECK(after == own, QStringLiteral("17.1: a redo whose pin read failed leaves the project's own version "
                                                   "(pin %1, own %2, library %3)").arg(after, own, library)
                                        .toUtf8().constData());
            }

            // 17.2 (F2) the pin's OID unread is no "no pin": MaterialBundle::write (the 1.5-s autosave) and
            //      ProjectAssets::updatePinToLatest never write the library's version over the project's own.
            {
                texture(QStringLiteral("t17w"));
                const QString library = AssetCas::sourceOid(conn, QStringLiteral("t17w"));
                const QString m17w = material(QStringLiteral("m17w"), QStringLiteral("t17w"));
                db.createProject(QStringLiteral("p17w"), QStringLiteral("p17w"));
                Project owner;
                owner.setProjectGuid(QStringLiteral("p17w"));
                AssetCas::writePin(conn, QStringLiteral("p17w"), m17w, AssetCas::sourceOid(conn, m17w));
                const QString own = painted(QStringLiteral("t17w"), owner);
                CHECK(!m17w.isEmpty() && !own.isEmpty() && own != library,
                      "17.2: CONTROL: a material the project holds, its member painted by the project");
                QJsonObject values;
                values[QStringLiteral("baseColorMap")] = QStringLiteral("t17w");
                values[QStringLiteral("roughness")] = 0.3;
                QJsonObject def;
                def[QStringLiteral("materialType")] = QStringLiteral("pbr");
                def[QStringLiteral("values")] = values;
                gDeny = Deny{ 5 };
                sqlite3_set_authorizer(raw, denyReads, nullptr);
                const MaterialBundle::WriteResult written =
                    MaterialBundle::write(&db, &owner, m17w, def, MaterialBundle::Scope::Project);
                sqlite3_set_authorizer(raw, nullptr, nullptr);
                const QString after = pinOf(QStringLiteral("p17w"), QStringLiteral("t17w"));
                CHECK(after == own, QStringLiteral("17.2: a write whose member's pin could not be read leaves the project's own "
                                                   "version (pin %1, own %2; write ok %3 '%4')")
                                        .arg(after, own).arg(written.ok).arg(written.error).toUtf8().constData());
                CHECK(!written.ok && written.error.contains(QStringLiteral("could not be read")),
                      QStringLiteral("17.2: ...and the write is REFUSED by name: '%1'").arg(written.error).toUtf8().constData());

                // (the sweep's site n) whether the project holds the MATERIAL, unread: the write is refused whole - the
                // project's pin of the material does not move (its unit rolled back).
                const QString materialPin = pinOf(QStringLiteral("p17w"), m17w);
                gDeny = Deny{ 4 };
                sqlite3_set_authorizer(raw, denyReads, nullptr);
                const MaterialBundle::WriteResult unheld =
                    MaterialBundle::write(&db, &owner, m17w, def, MaterialBundle::Scope::Project);
                sqlite3_set_authorizer(raw, nullptr, nullptr);
                CHECK(!unheld.ok && unheld.error.contains(QStringLiteral("holds the material could not be read"))
                          && pinOf(QStringLiteral("p17w"), m17w) == materialPin,
                      QStringLiteral("17.2n: a write whose 'does the project hold it' could not be read is refused whole "
                                     "(ok %1, '%2', the material's pin %3)")
                          .arg(unheld.ok).arg(unheld.error)
                          .arg(pinOf(QStringLiteral("p17w"), m17w) == materialPin ? QStringLiteral("kept")
                                                                                   : QStringLiteral("MOVED"))
                          .toUtf8().constData());

                // (the sweep's site j) a pin that could not be read resolves NOTHING - never the library's bytes.
                CHECK(!AssetCas::resolvePinned(conn, storeRoot, QStringLiteral("p17w"), QStringLiteral("t17w")).isEmpty(),
                      "17.2j: CONTROL: the project's version resolves");
                gDeny = Deny{ 5 };
                sqlite3_set_authorizer(raw, denyReads, nullptr);
                const QString unresolved = AssetCas::resolvePinned(conn, storeRoot, QStringLiteral("p17w"), QStringLiteral("t17w"));
                sqlite3_set_authorizer(raw, nullptr, nullptr);
                CHECK(unresolved.isEmpty(), QStringLiteral("17.2j: a pin read that FAILED resolves nothing (got '%1')")
                                                .arg(unresolved).toUtf8().constData());

                // (the sweep's site a) a member's library version unread: addToProject adds NOTHING - never an empty pin
                // over a real one.
                gDeny = Deny{ 6 };
                sqlite3_set_authorizer(raw, denyReads, nullptr);
                const ProjectAssets::Result added =
                    ProjectAssets::addToProject(m17w, &db, &owner, ProjectAssets::AddKind::Direct);
                sqlite3_set_authorizer(raw, nullptr, nullptr);
                const QString afterAdd = pinOf(QStringLiteral("p17w"), QStringLiteral("t17w"));
                CHECK(!afterAdd.isEmpty() && added.error.contains(QStringLiteral("could not be read")),
                      QStringLiteral("17.2a: an add whose library versions could not be read pins nothing (member pin '%1', "
                                     "'%2')").arg(afterAdd, added.error).toUtf8().constData());

                texture(QStringLiteral("t17u"));
                const QString m17u = material(QStringLiteral("m17u"), QStringLiteral("t17u"));
                AssetCas::writePin(conn, QStringLiteral("p17w"), m17u, AssetCas::sourceOid(conn, m17u));
                const QString ownU = painted(QStringLiteral("t17u"), owner);
                gDeny = Deny{ 5 };
                sqlite3_set_authorizer(raw, denyReads, nullptr);
                const bool updated = ProjectAssets::updatePinToLatest(m17u, &db, &owner);
                sqlite3_set_authorizer(raw, nullptr, nullptr);
                const QString afterU = pinOf(QStringLiteral("p17w"), QStringLiteral("t17u"));
                CHECK(afterU == ownU && !updated,
                      QStringLiteral("17.2: an update whose member's pin could not be read is REFUSED and leaves the project's "
                                     "own version (pin %1, own %2; answer %3)")
                          .arg(afterU, ownU).arg(updated).toUtf8().constData());
            }

            // 18. ADDING TO A PROJECT NEVER REVERTS THE PROJECT's OWN VERSION (the lead's ruling on B3's finding; every
            //     read succeeds here): a project paints a texture (copy-on-write), then adds a material whose closure
            //     holds that texture - the way a same-project paste, an avatar clip load or a binding re-adds it. The
            //     texture's pin must stay the project's own. "Update from Library" still moves it (the explicit door).
            {
                texture(QStringLiteral("t18"));
                const QString library = AssetCas::sourceOid(conn, QStringLiteral("t18"));
                const QString m18 = material(QStringLiteral("m18"), QStringLiteral("t18"));
                db.createProject(QStringLiteral("p18"), QStringLiteral("p18"));
                Project owner;
                owner.setProjectGuid(QStringLiteral("p18"));
                const ProjectAssets::Result first = ProjectAssets::addToProject(QStringLiteral("t18"), &db, &owner,
                                                                                ProjectAssets::AddKind::Binding);
                const QString own = painted(QStringLiteral("t18"), owner);
                CHECK(first.ok() && !own.isEmpty() && own != library && pinOf(QStringLiteral("p18"), QStringLiteral("t18")) == own,
                      "18: CONTROL: the project holds its OWN painted version of the texture");
                const ProjectAssets::Result again = ProjectAssets::addToProject(m18, &db, &owner, ProjectAssets::AddKind::Direct);
                const QString kept = pinOf(QStringLiteral("p18"), QStringLiteral("t18"));
                CHECK(again.ok() && again.pinnedGuids.contains(QStringLiteral("t18")) && kept == own,
                      QStringLiteral("18: adding a closure that holds it KEEPS the project's own version (pin %1, own %2, "
                                     "library %3)").arg(kept, own, library).toUtf8().constData());
                CHECK(pinOf(QStringLiteral("p18"), m18) == AssetCas::sourceOid(conn, m18),
                      "18: CONTROL: the material itself, new to the project, is pinned at the library's version");
                const bool updated = ProjectAssets::updatePinToLatest(QStringLiteral("t18"), &db, &owner);
                CHECK(updated && pinOf(QStringLiteral("p18"), QStringLiteral("t18")) == library,
                      "18: Update from Library (the explicit door) still MOVES the pin to the library's version");
                // Reset Material's redo after an undo still PINS the default's texture it took back (at the library's
                // version: the project had no pin of it).
                texture(QStringLiteral("t18r"));
                AssetCas::writePin(conn, QStringLiteral("p18"), QStringLiteral("t18r"),
                                   AssetCas::sourceOid(conn, QStringLiteral("t18r")));
                ResetMaterialCommand reset(&db, &owner, iris::MeshNode::create(), iris::MaterialPtr(),
                                           { QStringLiteral("t18r") }, { QStringLiteral("t18r") });
                reset.undo();
                reset.redo();
                CHECK(pinOf(QStringLiteral("p18"), QStringLiteral("t18r")) == AssetCas::sourceOid(conn, QStringLiteral("t18r"))
                          && db.isAssetPinnedBy(QStringLiteral("p18"), QStringLiteral("t18r")),
                      "18: Reset Material's redo still pins the texture its undo took back");
            }

            // 17.6 (F6) A SIDECAR THAT PARSES BUT THAT THE REBUILD SKIPS (no home in this format, no origin) keeps every
            //      object it lists through a forced sweep - beside a deleted asset's sidecar (one the rebuild places),
            //      which is collected with its object (the control).
            {
                const QString gcRoot = scratchDir.filePath(QStringLiteral("gc17_store"));
                QDir().mkpath(QDir(gcRoot).filePath(QStringLiteral("sidecar")));
                db::Options o;
                o.name = "gc17";
                db::Error e;
                db::Connection gc = db::Connection::open(scratchDir.filePath(QStringLiteral("gc17.db")).toStdString(), o, &e);
                gc.exec(CasSchema::kAssetsTableForRebuild, &e);
                AssetCas::ensureCasSchema(gc);
                const auto object = [&gcRoot](const char *oid) {
                    const QString path = AssetStorePaths::objectPathIn(gcRoot, QString::fromLatin1(oid), QStringLiteral("png"));
                    QDir().mkpath(QFileInfo(path).absolutePath());
                    QFile f(path);
                    f.open(QIODevice::WriteOnly);
                    f.write("bytes");
                    return path;
                };
                const auto sidecar = [&gcRoot](const char *guid, QJsonObject body, const char *oid) {
                    body[QStringLiteral("guid")] = QString::fromLatin1(guid);
                    QJsonObject file;
                    file[QStringLiteral("oid")] = QString::fromLatin1(oid);
                    file[QStringLiteral("ext")] = QStringLiteral("png");
                    file[QStringLiteral("role")] = QStringLiteral("source");
                    file[QStringLiteral("name")] = QStringLiteral("x.png");
                    body[QStringLiteral("files")] = QJsonArray{ file };
                    const QString path = QDir(gcRoot).filePath(QStringLiteral("sidecar/%1.json").arg(QString::fromLatin1(guid)));
                    QFile f(path);
                    f.open(QIODevice::WriteOnly);
                    f.write(QJsonDocument(body).toJson());
                    return path;
                };
                QJsonObject placed;
                placed[QStringLiteral("formatVersion")] = 3;
                placed[QStringLiteral("home")] = assethome::kindName(assethome::Kind::MaterialsLibrary);
                placed[QStringLiteral("origin")] = QStringLiteral("create");
                QJsonObject noHome;
                noHome[QStringLiteral("formatVersion")] = 2;
                QJsonObject noOrigin = placed;
                noOrigin.remove(QStringLiteral("origin"));
                const char *goneOid = "cc17000000000000000000000000000000000000";
                const char *oldOid = "dd17000000000000000000000000000000000000";
                const char *originOid = "ee17000000000000000000000000000000000000";
                const QString goneObject = object(goneOid), goneSidecar = sidecar("gone17", placed, goneOid);
                const QString oldObject = object(oldOid), oldSidecar = sidecar("old17", noHome, oldOid);
                const QString originObject = object(originOid), originSidecar = sidecar("noorigin17", noOrigin, originOid);
                const AssetGc::Report swept = AssetGc::sweep(gc, gcRoot, /*dryRun*/ false, /*force*/ true);
                CHECK(swept.ok && !QFile::exists(goneObject) && !QFile::exists(goneSidecar),
                      QStringLiteral("17.6: CONTROL: a deleted asset's sidecar and its object ARE collected (ok %1, '%2')")
                          .arg(swept.ok).arg(swept.error).toUtf8().constData());
                CHECK(QFile::exists(oldObject) && QFile::exists(oldSidecar) && QFile::exists(originObject)
                          && QFile::exists(originSidecar),
                      QStringLiteral("17.6: sidecars the rebuild cannot place, and the objects they list, SURVIVE a forced "
                                     "sweep (no home: object %1 sidecar %2; no origin: object %3 sidecar %4)")
                          .arg(QFile::exists(oldObject)).arg(QFile::exists(oldSidecar))
                          .arg(QFile::exists(originObject)).arg(QFile::exists(originSidecar)).toUtf8().constData());
                QStringList named;
                for (const AssetGc::Item &item : swept.unplacedSidecars.items) named << item.id + QStringLiteral(": ") + item.reason;
                named.sort();
                CHECK(named.size() == 2 && named.at(0).startsWith(QStringLiteral("noorigin17: "))
                          && named.at(0).contains(QStringLiteral("no origin in this format"))
                          && named.at(1).startsWith(QStringLiteral("old17: "))
                          && named.at(1).contains(QStringLiteral("no home in this format")),
                      QStringLiteral("17.6: ...each NAMED with the rebuild's own reason: %1").arg(named.join(QStringLiteral(" | ")))
                          .toUtf8().constData());
            }
        }

        // (2) A CLOSED LIBRARY's pin reads are NO ANSWER, never "no pins". LAST in this test: a second Database
        //     takes Qt's DEFAULT connection name and so closes the first one's handle (stage 3 removes that).
        {
            Database closed;
            closed.initializeDatabase(scratchDir.filePath("closed_probe.db"));
            closed.createAllTables();
            closed.closeDatabase();
            bool countRead = true, listRead = true;
            closed.countAssetPins(QStringLiteral("any"), &countRead);
            closed.fetchAssetPins(QStringLiteral("any"), &listRead);
            CHECK(!countRead && !listRead, "14.2: a closed library's pin count and pin list answer NO READ (never 0 pins)");
        }
    }

    printf(failures ? "\n%d FAILURES\n" : "\nall material bundle assertions passed\n", failures);
    return failures ? 1 : 0;
}
