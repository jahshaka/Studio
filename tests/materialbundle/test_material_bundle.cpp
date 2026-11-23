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
struct Deny { int mode = 0; int selects = 0; bool inSelect = false; int probesDenied = 0; };
static Deny gDeny;
static int denyReads(void *, int action, const char *table, const char *, const char *, const char *)
{
    if (action == SQLITE_SELECT) { gDeny.inSelect = true; ++gDeny.selects; return SQLITE_OK; }
    if (action == SQLITE_INSERT || action == SQLITE_UPDATE || action == SQLITE_DELETE) gDeny.inSelect = false;
    if (action != SQLITE_READ || !table) return SQLITE_OK;
    const bool master = std::strcmp(table, "sqlite_master") == 0 || std::strcmp(table, "sqlite_schema") == 0;
    if (gDeny.mode == 1 && master) return SQLITE_DENY;
    if (gDeny.mode == 2 && gDeny.inSelect && std::strcmp(table, "projects") == 0) return SQLITE_DENY;
    if (gDeny.mode == 3 && master && gDeny.selects >= 7) { ++gDeny.probesDenied; return SQLITE_DENY; }
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
            CHECK(!gc.ok || !listed, "14.5: a pin-only object is never listed unreferenced from a failed pin-table probe");
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
            CHECK(rebuilt.assets == 1, QStringLiteral("14.1: CONTROL: the good row was rebuilt (%1)").arg(rebuilt.assets)
                                           .toUtf8().constData());
            CHECK(!rebuilt.ok, QStringLiteral("14.1: a rebuild with a FAILED row is not ok (ok %1, error '%2')")
                                   .arg(rebuilt.ok).arg(rebuilt.error).toUtf8().constData());
            CHECK(rebuilt.failedRows == 1 && rebuilt.failedGuids == QStringList{ QStringLiteral("r1-poisoned") }
                      && rebuilt.error.contains(QStringLiteral("r1-poisoned")) && rebuilt.committed,
                  QStringLiteral("14.1: ...the failed row is COUNTED and NAMED (%1: %2)").arg(rebuilt.failedRows)
                      .arg(rebuilt.failedGuids.join(QLatin1Char(','))).toUtf8().constData());
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
