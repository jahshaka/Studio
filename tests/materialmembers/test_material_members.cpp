// THE BUNDLE'S MEMBERS — MATERIAL_BUNDLE_SPEC phase 2 (§3.3 V-2, §4, §6),
// against the REAL Database and the REAL content-addressed store on a
// throwaway SQLite file. No UI, no engine: this is the model the Members
// panel and the verbs both read.
//
// What it pins, each an owner-visible claim:
//
//   1. §6 — `describe` is the Members panel's row: name, the SLOT it fills or
//      the graph NODE that holds it, baked or source, size, used-by, pinned.
//      "Used by" counts every user, not one project's.
//   2. V-2 (owner Q4) — a texture that arrived THROUGH a material's picker is
//      stamped and folds into the bundle; a texture the USER imported is
//      always a tile; a stamped one comes back the moment anything but a
//      material uses it. The stamp is an ORIGIN, not "something depends on
//      it" — the dependency-hiding of 2026-09-12 is not this rule.
//   3. §4 L-2 (owner Q6) — `unused` lists before anything goes, and the two
//      scopes differ: a bundle's own born-inside row with no user and no pin
//      goes from the LIBRARY; a member pin nothing in the project uses drops
//      the PIN only. Bytes are never touched here.
//   4. §4 — `makeUnique` gives one material its own row over the SAME bytes:
//      the store gains no object, the definition names the new row
//      everywhere (slot, bake record, graph node), and a material that shared
//      the picture keeps it.
//   5. F20 (phase 1's code review) — "Update to latest" on a bundle never
//      discards a member the project copied on write, and never overwrites a
//      pin with an empty oid.
//
// Framework-free; non-zero exit on failure. Runs displayless.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDirIterator>
#include <QTemporaryDir>
#include <cstdio>

#include "data/database/database.h"
#include "data/constants.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetdelete.h"
#include "services/assetstorepaths.h"
#include "services/materialbundle.h"
#include "services/materialmembers.h"
#include "services/projectassets.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

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

static int countWhere(const QString &sql, const QVariantList &binds = {})
{
    QSqlQuery q;
    q.prepare(sql);
    for (const auto &b : binds) q.addBindValue(b);
    if (!q.exec()) { printf("info: query error: %s\n", qPrintable(q.lastError().text())); return -1; }
    return q.next() ? q.value(0).toInt() : -1;
}

static int objectCount(const QString &storeRoot)
{
    int n = 0;
    QDirIterator it(QDir(storeRoot).filePath("objects"), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); ++n; }
    return n;
}

static const materialmembers::Member *findMember(const QVector<materialmembers::Member> &list,
                                                 const QString &guid)
{
    for (const auto &m : list) if (m.guid == guid) return &m;
    return nullptr;
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

    const QString dbPath = scratchDir.filePath("materialmembers_test.db");
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();
    QSqlDatabase conn = QSqlDatabase::database();

    // --- fixtures ---------------------------------------------------------
    const QString woodSrc  = writeTempFile(scratchDir, "wood.jpg",  QByteArray("wood-bytes"));
    const QString brickSrc = writeTempFile(scratchDir, "brick.jpg", QByteArray("brick-bytes"));
    const QString bakeSrc  = writeTempFile(scratchDir, "baked.png", QByteArray("baked-bytes"));
    const QString mineSrc  = writeTempFile(scratchDir, "mine.png",  QByteArray("mine-bytes"));
    textureRow(db, "tex-wood",  "wood.jpg",  storeRoot, woodSrc);
    textureRow(db, "tex-brick", "brick.jpg", storeRoot, brickSrc);
    textureRow(db, "tex-baked", "baked.png", storeRoot, bakeSrc);
    textureRow(db, "tex-mine",  "mine.png",  storeRoot, mineSrc);

    // A graph material with a picked picture in a slot, a BAKED map, and a
    // texture held only by a graph node.
    QJsonObject values;
    values["baseColorMap"] = "tex-wood";
    values["normalMap"] = "tex-baked";
    QJsonObject bakeMaps;
    bakeMaps["normalMap"] = "tex-baked";
    QJsonObject bake;
    bake["maps"] = bakeMaps;
    QJsonObject node;
    node["type"] = "texture";
    node["id"] = "node-7";
    node["value"] = "tex-brick";
    QJsonArray nodes;
    nodes.append(node);
    QJsonObject graph;
    graph["nodes"] = nodes;
    QJsonObject definition;
    definition["materialType"] = "pbr";
    definition["values"] = values;
    definition["bake"] = bake;
    definition["shadergraph"] = graph;

    QString error;
    const QString woody = MaterialBundle::create(&db, "woody", definition, QByteArray(), &error);
    CHECK(!woody.isEmpty(), qPrintable(QStringLiteral("a bundle was minted (%1)").arg(error)));

    // =======================================================================
    // 1. THE MEMBERS PANEL'S ROW
    // =======================================================================
    const auto members = materialmembers::describe(&db, nullptr, woody);
    CHECK(members.size() == 3, "1: three members — the slot picture, the baked map, the graph node's");

    const auto *wood = findMember(members, "tex-wood");
    CHECK(wood && wood->slot == "baseColorMap", "1: the picked picture reports its SLOT");
    CHECK(wood && wood->role == "source", "1: ...and reads as a source picture, not a bake");
    CHECK(wood && wood->bytes == QByteArray("wood-bytes").size(),
          "1: its size is the stored object's");

    const auto *baked = findMember(members, "tex-baked");
    CHECK(baked && baked->role == "baked", "1: the bake record's map reads as BAKED");

    const auto *brick = findMember(members, "tex-brick");
    CHECK(brick && brick->slot.isEmpty() && brick->node == "node-7",
          "1: a texture only a graph node holds reports the NODE, not a slot");

    CHECK(wood && wood->usedBy == 1, "1: used by 1 — this material");

    // A SECOND material on the same picture: used by 2, from either side.
    QJsonObject shared;
    shared["materialType"] = "pbr";
    QJsonObject sharedValues;
    sharedValues["baseColorMap"] = "tex-wood";
    shared["values"] = sharedValues;
    const QString planks = MaterialBundle::create(&db, "planks", shared, QByteArray(), &error);
    CHECK(!planks.isEmpty(), "1: a second bundle on the same picture");
    CHECK(materialmembers::usedBy(&db, "tex-wood") == 2,
          "1: 'used by' counts EVERY material that names it (one object, two bundles)");

    // =======================================================================
    // 2. V-2 — WHICH TEXTURES GET A TILE
    // =======================================================================
    CHECK(!materialmembers::hiddenAsMember(&db, "tex-wood"),
          "2: an UNSTAMPED texture is a tile even when only materials use it (V-3 is not our rule)");
    CHECK(materialmembers::stampMember(&db, "tex-wood", woody), "2: the picker stamps its import");
    CHECK(materialmembers::isStampedMember(&db, "tex-wood"), "2: the stamp reads back");
    CHECK(materialmembers::hiddenAsMember(&db, "tex-wood"),
          "2: a stamped picture only materials use folds into the bundle");
    CHECK(!materialmembers::hiddenAsMember(&db, "tex-mine"),
          "2: a texture the user imported themselves is never folded");

    // A SCENE NODE uses it directly: the depender is not a catalog row, so the
    // picture is a thing of the user's again.
    db.createDependency(static_cast<int>(ModelTypes::Object),
                        static_cast<int>(ModelTypes::Texture),
                        "some-scene-node", "tex-wood", "proj-1");
    CHECK(!materialmembers::hiddenAsMember(&db, "tex-wood"),
          "2: ...and comes BACK as a tile the moment a scene node uses it");
    db.deleteDependency("some-scene-node", "tex-wood");
    CHECK(materialmembers::hiddenAsMember(&db, "tex-wood"), "2: folded again once that use is gone");

    // =======================================================================
    // 3. UNUSED — LISTED FIRST, AND THE TWO SCOPES
    // =======================================================================
    // A picture picked into `woody` and then dropped from its definition: the
    // edges went with the slot, only the origin stamp remains.
    textureRow(db, "tex-orphan", "orphan.png", storeRoot,
               writeTempFile(scratchDir, "orphan.png", QByteArray("orphan-bytes")));
    CHECK(materialmembers::stampMember(&db, "tex-orphan", woody), "3: it was picked into woody");

    auto listed = materialmembers::unused(&db, nullptr, woody);
    CHECK(listed.size() == 1 && listed.first().guid == "tex-orphan",
          "3: the dry run lists exactly the row nothing uses");
    CHECK(listed.first().scope == "library", "3: a bundle-scope clean is a LIBRARY row");
    CHECK(!db.fetchAsset("tex-orphan").guid.isEmpty(),
          "3: ...and the dry run removed NOTHING");

    CHECK(materialmembers::unused(&db, nullptr, planks).isEmpty(),
          "3: another bundle's clean does not reach woody's rows");

    const int before = objectCount(storeRoot);
    const auto cleaned = materialmembers::cleanUnused(&db, nullptr, woody, &error);
    CHECK(cleaned.size() == 1, "3: the clean removed the one row it listed");
    CHECK(db.fetchAsset("tex-orphan").guid.isEmpty(), "3: the row is gone from the library");
    CHECK(objectCount(storeRoot) == before,
          "3: the BYTES are untouched — a superseded object is assets.gc's business");
    CHECK(materialmembers::unused(&db, nullptr, woody).isEmpty(), "3: nothing left to clean");

    // =======================================================================
    // 4. MAKE UNIQUE — A SECOND ROW OVER THE SAME BYTES
    // =======================================================================
    const int objectsBefore = objectCount(storeRoot);
    const QString mine = materialmembers::makeUnique(&db, nullptr, woody, "tex-wood", &error);
    CHECK(!mine.isEmpty(), qPrintable(QStringLiteral("4: woody got its own row (%1)").arg(error)));
    CHECK(mine != "tex-wood", "4: ...and it is a NEW row");
    // The store gained the material's new DEFINITION and nothing else: no
    // second copy of the picture exists, because the object is named by its
    // hash and both rows point at the one that is already there.
    CHECK(objectCount(storeRoot) == objectsBefore + 1,
          "4: the store gained only the new definition — NO second copy of the picture");
    CHECK(AssetCas::sourceOid(conn, mine) == AssetCas::sourceOid(conn, "tex-wood"),
          "4: both rows name the same object");

    const QJsonObject after = MaterialBundle::read(&db, woody, nullptr);
    CHECK(after.value("values").toObject().value("baseColorMap").toString() == mine,
          "4: woody's slot names the new row");
    const QJsonObject stillShared = MaterialBundle::read(&db, planks, nullptr);
    CHECK(stillShared.value("values").toObject().value("baseColorMap").toString() == "tex-wood",
          "4: the material that SHARED the picture keeps it and never notices");
    CHECK(materialmembers::isStampedMember(&db, mine),
          "4: the copy is a member of the material that asked for it");

    // The graph node's texture too, to prove every place is rewritten.
    const QString brickCopy = materialmembers::makeUnique(&db, nullptr, woody, "tex-brick", &error);
    CHECK(!brickCopy.isEmpty(), "4: a graph node's texture can be made unique too");
    const QJsonObject afterNode = MaterialBundle::read(&db, woody, nullptr);
    CHECK(afterNode.value("shadergraph").toObject().value("nodes").toArray()
              .first().toObject().value("value").toString() == brickCopy,
          "4: the graph NODE names the new row");

    CHECK(materialmembers::makeUnique(&db, nullptr, planks, "tex-baked", &error).isEmpty(),
          "4: a texture that is not a member of that material is refused");

    // =======================================================================
    // 5. F20 — UPDATE TO LATEST KEEPS THE PROJECT'S OWN VERSION OF A MEMBER
    // =======================================================================
    Project project;
    project.setProjectGuid("proj-1");
    CHECK(db.createProject("proj-1", "Bundle Project"), "5: fixture project row");

    CHECK(ProjectAssets::addToProject(planks, &db, &project,
                                      ProjectAssets::AddKind::Direct).ok(),
          "5: the project takes the bundle and its closure");
    const QString pinnedMaterial = AssetCas::pinnedOid(conn, "proj-1", planks);
    CHECK(!pinnedMaterial.isEmpty(), "5: the material is pinned at the version it was added");

    // The project paints on its texture: copy-on-write moves THIS project's
    // pin and leaves the library's row where it was.
    // THE EDITED FILE KEEPS THE ASSET'S OWN NAME, as a real copy-on-write
    // does: `ingestFile`'s asset_files key is (guid, role, NAME), so an edit
    // staged under a different name adds a SECOND source row and moves what
    // the LIBRARY points at — which is not what editing inside a project
    // means.
    QDir().mkpath(scratchDir.filePath("edit"));
    const QString editedSrc =
        writeTempFile(QDir(scratchDir.filePath("edit")), "wood.jpg", QByteArray("painted"));
    const QString editedOid = ProjectAssets::copyOnWrite("tex-wood", editedSrc, &db, &project, &error);
    CHECK(!editedOid.isEmpty(), "5: the project copies its texture on write");
    CHECK(AssetCas::pinnedOid(conn, "proj-1", "tex-wood") == editedOid,
          "5: the project's pin names the painted bytes");

    // ...and the library material is edited, so the project is offered a newer
    // version of the BUNDLE.
    QJsonObject newer = MaterialBundle::read(&db, planks, nullptr);
    QJsonObject newerValues = newer.value("values").toObject();
    newerValues["roughness"] = 0.25;
    newer["values"] = newerValues;
    CHECK(MaterialBundle::write(&db, nullptr, planks, newer, MaterialBundle::Scope::Library).ok,
          "5: the library publishes a newer definition");

    CHECK(ProjectAssets::updatePinToLatest(planks, &db, &project), "5: the project takes it");
    CHECK(AssetCas::pinnedOid(conn, "proj-1", planks) == AssetCas::sourceOid(conn, planks),
          "5: the bundle's own pin moved to the library's current version");
    CHECK(AssetCas::pinnedOid(conn, "proj-1", "tex-wood") == editedOid,
          "5: THE PAINTED TEXTURE SURVIVED — an update of the material is not consent to "
          "discard the user's own version of a member (F20)");

    // A member the NEW version adds is pinned by the same call (audit G3).
    QJsonObject withNormal = MaterialBundle::read(&db, planks, nullptr);
    QJsonObject withNormalValues = withNormal.value("values").toObject();
    withNormalValues["normalMap"] = "tex-mine";
    withNormal["values"] = withNormalValues;
    CHECK(MaterialBundle::write(&db, nullptr, planks, withNormal, MaterialBundle::Scope::Library).ok,
          "5: the library adds a member");
    CHECK(!db.isAssetPinnedBy("proj-1", "tex-mine"), "5: the project does not hold it yet");
    CHECK(ProjectAssets::updatePinToLatest(planks, &db, &project), "5: update again");
    CHECK(db.isAssetPinnedBy("proj-1", "tex-mine"),
          "5: ...and the member the new version ADDS is pinned with it (G3)");

    // =======================================================================
    // 6. A PROJECT-SCOPE WRITE: EDGES ONCE, AND "USED BY" COUNTS USERS (F3)
    // =======================================================================
    //
    // No suite drove a project-scope definition write before this one, and
    // that is exactly where the double count lived: the write derives the
    // project's own edge set while the library's intrinsic edge stays, so a
    // texture ONE material uses had TWO rows naming it — and the Members
    // panel said "used by 2", the tooltip said "shared with 1 other" and
    // Make unique lit up, inviting a duplicate nobody needs.
    {
        // `planks` is pinned by proj-1 from section 5 and names tex-wood.
        CHECK(db.isAssetPinnedBy("proj-1", planks), "6: the project holds the bundle");
        QJsonObject projectEdit = MaterialBundle::read(&db, planks, &project);
        QJsonObject editedValues = projectEdit.value("values").toObject();
        editedValues["roughness"] = 0.9;
        projectEdit["values"] = editedValues;
        const auto written = MaterialBundle::write(&db, &project, planks, projectEdit,
                                                   MaterialBundle::Scope::Project);
        CHECK(written.ok, qPrintable(QStringLiteral("6: the project-scope save wrote (%1)")
                                         .arg(written.error)));

        // The library's intrinsic edge AND the project's own edge both exist —
        // that is the design (the library version must not move).
        CHECK(countWhere("SELECT COUNT(*) FROM dependencies WHERE depender = ? AND dependee = ? "
                         "AND project_guid IS NULL", { planks, "tex-wood" }) == 1,
              "6: the library's intrinsic edge survived the project's save");
        CHECK(countWhere("SELECT COUNT(*) FROM dependencies WHERE depender = ? AND dependee = ? "
                         "AND project_guid = ?", { planks, "tex-wood", "proj-1" }) == 1,
              "6: and the project has one of its own");

        // ...and the COUNT the user sees is of USERS, not of edge rows.
        CHECK(materialmembers::usedBy(&db, "tex-wood") == 1,
              "6: 'used by' says ONE material, not two (F3: the query is DISTINCT)");
        const auto shown = materialmembers::describe(&db, &project, planks);
        const auto *woodRow = findMember(shown, "tex-wood");
        CHECK(woodRow && woodRow->usedBy == 1, "6: the Members panel's row agrees");
    }

    // =======================================================================
    // 7. AN UNRELATED SAVE DOES NOT RESET A MEMBER'S PIN (F4)
    // =======================================================================
    //
    // The twin of section 5's loss, and the more dangerous one: this runs on
    // the graph page's 1.5 s AUTOSAVE, so an edit to the MATERIAL used to put
    // a texture the project had copied on write back to the library's bytes,
    // repeatedly, with nothing on screen to connect the two.
    {
        const QString painted = AssetCas::pinnedOid(conn, "proj-1", "tex-wood");
        CHECK(!painted.isEmpty() && painted != AssetCas::sourceOid(conn, "tex-wood"),
              "7: the project renders its OWN version of the texture");

        QJsonObject edit = MaterialBundle::read(&db, planks, &project);
        QJsonObject values = edit.value("values").toObject();
        values["metallic"] = 0.5;            // nothing to do with the texture
        edit["values"] = values;
        CHECK(MaterialBundle::write(&db, &project, planks, edit,
                                    MaterialBundle::Scope::Project).ok,
              "7: an ordinary edit to the material is saved");
        CHECK(AssetCas::pinnedOid(conn, "proj-1", "tex-wood") == painted,
              "7: THE PAINTED TEXTURE IS STILL WHAT THE PROJECT RENDERS (F4)");

        // What the loop IS for: a member with no pin yet gets one.
        textureRow(db, "tex-fresh", "fresh.png", storeRoot,
                   writeTempFile(scratchDir, "fresh.png", QByteArray("fresh-bytes")));
        QJsonObject withFresh = MaterialBundle::read(&db, planks, &project);
        QJsonObject freshValues = withFresh.value("values").toObject();
        freshValues["emissiveMap"] = "tex-fresh";
        withFresh["values"] = freshValues;
        CHECK(!db.isAssetPinnedBy("proj-1", "tex-fresh"), "7: the new member is unpinned");
        CHECK(MaterialBundle::write(&db, &project, planks, withFresh,
                                    MaterialBundle::Scope::Project).ok,
              "7: the save that adds it");
        CHECK(db.isAssetPinnedBy("proj-1", "tex-fresh"),
              "7: ...and the member the project did not hold IS pinned by that save");
    }

    // =======================================================================
    // 8. DUPLICATE: THE PICTURES ARE SHARED, THE BAKE IS NOT (F1/F5)
    // =======================================================================
    {
        // A bundle with a picked picture AND a baked map, like a saved graph.
        textureRow(db, "tex-bake2", "bake2.png", storeRoot,
                   writeTempFile(scratchDir, "bake2.png", QByteArray("bake2-bytes")));
        QJsonObject bakeValues;
        bakeValues["baseColorMap"] = "tex-mine";      // a picture
        bakeValues["normalMap"] = "tex-bake2";        // a bake's output
        QJsonObject maps2; maps2["normalMap"] = "tex-bake2";
        QJsonObject bake2; bake2["maps"] = maps2;
        QJsonObject graphed;
        graphed["materialType"] = "pbr";
        graphed["values"] = bakeValues;
        graphed["bake"] = bake2;
        const QString original = MaterialBundle::create(&db, "Graphed", graphed, QByteArray(), &error);
        CHECK(!original.isEmpty(), "8: a bundle with a picture and a baked map");

        QString dupError;
        const QString copy = materialmembers::duplicate(&db, nullptr, original, QString(), &dupError);
        CHECK(!copy.isEmpty(), qPrintable(QStringLiteral("8: it duplicates (%1)").arg(dupError)));
        CHECK(db.fetchAsset(copy).name == "Graphed copy", "8: named '<original> copy'");

        const QJsonObject copied = MaterialBundle::read(&db, copy, nullptr);
        CHECK(copied.value("values").toObject().value("baseColorMap").toString() == "tex-mine",
              "8: the PICTURE is shared — one object, two materials");
        CHECK(materialmembers::usedBy(&db, "tex-mine") >= 2, "8: ...which is what 'used by' says");
        CHECK(!copied.contains("bake"),
              "8: the BAKE is NOT inherited (a baked map belongs to one material)");
        CHECK(!copied.value("values").toObject().contains("normalMap"),
              "8: and the slot it filled is clear, to be re-baked by the copy's own save");
        CHECK(materialmembers::usedBy(&db, "tex-bake2") == 1,
              "8: the original's baked map is still the original's alone");

        // A second duplicate does not collide with the first's name.
        const QString second = materialmembers::duplicate(&db, nullptr, original, QString(), &dupError);
        CHECK(!second.isEmpty() && db.fetchAsset(second).name == "Graphed copy 2",
              "8: a second copy is numbered, never a duplicate name");

        // AND THE LIBRARY DELETE TAKES THE BUNDLE'S OWN MEMBERS (F6), BOTH
        // KINDS. A PICKED picture is found by its origin stamp; a BAKED map
        // is found by its PARENT, because a parented row appears in no
        // library listing and the stamp walk cannot see it — which is
        // precisely how a baked map used to outlive its material for ever.
        materialmembers::stampMember(&db, "tex-bake2", original);   // the picked one
        db.createAssetEntry("tex-bakedchild", "child.png",
                            static_cast<int>(ModelTypes::Texture), original, QString(),
                            QString(), QString(), QByteArray(), QByteArray(), QByteArray(),
                            QByteArray(), AssetViewFilter::AssetsView);
        CHECK(!db.fetchAsset("tex-bakedchild").guid.isEmpty(),
              "8: a BAKED member row, parented to the material");

        CHECK(assetdelete::remove(&db, original).ok, "8: the original is deleted from the library");
        materialmembers::reapExclusiveMembers(&db, original);
        CHECK(db.fetchAsset("tex-bake2").guid.isEmpty(),
              "8: its stamped born-inside picture went with it (F6)");
        CHECK(db.fetchAsset("tex-bakedchild").guid.isEmpty(),
              "8: and so did its BAKED member, which no listing could have shown");
        CHECK(!db.fetchAsset("tex-mine").guid.isEmpty(),
              "8: the picture the copy still uses did NOT");
    }

    printf(failures ? "\n%d CHECK(s) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
