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
    const QString editedSrc = writeTempFile(scratchDir, "wood-edited.jpg", QByteArray("painted"));
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

    printf(failures ? "\n%d CHECK(s) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
