// Headless characterisation test for the asset-drawers storage layer
// (ASSET_DRAWERS_SPEC.md §2/§5): the fresh collections schema (an older one is refused),
// drawer CRUD + nesting, delete-reassigns-to-Uncategorized, and cycle
// rejection on reparent.
//
// Builds the REAL Database class (src/data/database/database.cpp) against a
// throwaway SQLite file in the test working directory — the user's live
// JahLibrary.db is never touched. Runs under QT_QPA_PLATFORM=offscreen.
// Framework-free; non-zero exit on failure.
#include <QApplication>
#include <QFile>
#include <cstdio>

#include "data/database/database.h"
#include "data/project.h"
#include "../support/testsql.h"

static int failures = 0;
/// The suite's library connection (main sets it once the Database is open): its own statements run on it.
static db::Connection *gLibrary = nullptr;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

// find a collection by id in a fetch result; -2 parent means "not found"
static CollectionRecord findCollection(const QVector<CollectionRecord> &colls, int id)
{
    for (const auto &c : colls) if (c.id == id) return c;
    CollectionRecord none; none.parent = -2;
    return none;
}

static int collectionOfAsset(const QString &guid)
{
    const std::int64_t collection =
        testsql::integer(*gLibrary, QStringLiteral("SELECT collection FROM assets WHERE guid = ?"), guid);
    return collection < 0 ? -999 : int(collection);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets (QMessageBox)

    const QString dbPath = "drawers_test.db";
    QFile::remove(dbPath);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    gLibrary = &db.connection();

    // FSYNC-2: the library opens in WAL with synchronous=NORMAL, so a catalog
    // write never makes its thread wait for the device. That thread is the UI
    // thread (every Database method rides the library's connection), and at the
    // SQLite defaults one commit measured 707 ms of frozen window on this box.
    // Integrity is untouched — WAL recovers to a transaction boundary from any
    // crash; what it gives up is the last few seconds of catalog writes after a
    // POWER CUT, which the store's sidecars can rebuild (database.cpp).
    {
        const QString mode = testsql::text(db.connection(), QStringLiteral("PRAGMA journal_mode")).toLower();
        CHECK(mode == QStringLiteral("wal"), "the library database is in WAL mode");
        // 0 = OFF, 1 = NORMAL, 2 = FULL.
        const std::int64_t level = testsql::integer(db.connection(), QStringLiteral("PRAGMA synchronous"));
        CHECK(level == 1, "... at synchronous = NORMAL");
    }

    // --- FORWARD-ONLY-1: a PRE-DRAWERS library is REFUSED by the schema
    //     check, never migrated (main() wipes it).
    {
        const bool ok = testsql::exec(db.connection(),
            "CREATE TABLE collections ("
            "    name VARCHAR(128), date_created DATETIME DEFAULT CURRENT_TIMESTAMP,"
            "    collection_id INTEGER PRIMARY KEY)");
        CHECK(ok, "a pre-drawers collections table created");
        CHECK(!db.schemaMatchesFresh(), "the schema check REFUSES a pre-drawers library");
        CHECK(testsql::exec(db.connection(), "DROP TABLE collections"), "...and it is dropped (the wipe's job)");
    }
    db.createAllTables();
    CHECK(db.checkIfColumnExists("collections", "parent"), "the fresh schema has the parent column");
    {
        const bool ok = testsql::exec(db.connection(),
                                      "INSERT INTO collections (name, date_created) VALUES ('Legacy Props', datetime())");
        CHECK(ok, "a collection inserted with no parent given");
        auto colls = db.fetchCollections();
        CHECK(colls.size() == 2, "the seeded Uncategorized and the new one are listed");
        CHECK(findCollection(colls, 0).parent == -1, "Uncategorized reads parent -1 (top level)");
        bool topLevel = false;
        for (const auto &c : colls)
            if (c.name == "Legacy Props" && c.parent == -1) topLevel = true;
        CHECK(topLevel, "a collection with no parent sits at top level");
    }

    // --- CRUD + nesting -----------------------------------------------------
    const int props = db.createCollection("Props");
    CHECK(props > 0, "createCollection at top level -> id");
    const int weapons = db.createCollection("Weapons", props);
    const int swords = db.createCollection("Swords", weapons);
    CHECK(weapons > 0 && swords > 0, "nested drawers created");
    CHECK(db.createCollection("Orphan", 424242) == -1, "createCollection under unknown parent refused");

    {
        auto colls = db.fetchCollections();
        CHECK(findCollection(colls, props).parent == -1, "Props at top level");
        CHECK(findCollection(colls, weapons).parent == props, "Weapons under Props");
        CHECK(findCollection(colls, swords).parent == weapons, "Swords under Weapons");
    }

    CHECK(db.renameCollection(weapons, "Weaponry"), "renameCollection succeeds");
    CHECK(findCollection(db.fetchCollections(), weapons).name == "Weaponry", "rename persisted");

    {
        auto subtree = db.fetchCollectionSubtree(props);
        CHECK(subtree.size() == 3 && subtree.contains(props) && subtree.contains(weapons)
                  && subtree.contains(swords), "fetchCollectionSubtree spans the branch");
        CHECK(db.fetchCollectionSubtree(-1).isEmpty(), "virtual root (-1) is not a row");
        CHECK(db.fetchCollectionSubtree(424242).isEmpty(), "unknown drawer -> empty subtree");
    }

    // --- Reparent + cycle rejection ----------------------------------------
    const int scenery = db.createCollection("Scenery");
    CHECK(db.setCollectionParent(swords, scenery), "reparent Swords under Scenery");
    CHECK(findCollection(db.fetchCollections(), swords).parent == scenery, "reparent persisted");
    CHECK(db.setCollectionParent(swords, -1), "reparent to top level");
    CHECK(findCollection(db.fetchCollections(), swords).parent == -1, "top-level reparent persisted");
    CHECK(db.setCollectionParent(swords, weapons), "reparent back under Weaponry");

    CHECK(!db.setCollectionParent(props, props), "self-parent refused");
    CHECK(!db.setCollectionParent(props, swords), "cycle refused (own descendant)");
    CHECK(!db.setCollectionParent(props, 424242), "unknown parent refused");
    CHECK(!db.setCollectionParent(424242, props), "unknown drawer refused");
    CHECK(!db.setCollectionParent(0, props), "Uncategorized cannot be reparented");
    CHECK(!db.setCollectionParent(-1, props), "the virtual root cannot be reparented");
    CHECK(findCollection(db.fetchCollections(), props).parent == -1, "failed moves changed nothing");

    // --- Delete reassigns the subtree's assets to Uncategorized -------------
    const QString swordGuid = db.createAssetEntry(
        "guid-sword-01", "sword.obj", static_cast<int>(ModelTypes::Object),
        QString(), assethome::assets(), assethome::Origin::Import, QString(), QString(), QByteArray(), QByteArray(),
        QByteArray(), QByteArray());
    const QString shieldGuid = db.createAssetEntry(
        "guid-shield-01", "shield.obj", static_cast<int>(ModelTypes::Object),
        QString(), assethome::assets(), assethome::Origin::Import, QString(), QString(), QByteArray(), QByteArray(),
        QByteArray(), QByteArray());
    CHECK(!swordGuid.isEmpty() && !shieldGuid.isEmpty(), "asset rows created");

    // --- THE DOOR RULE (ASSETS-HOME-1): Assets takes only imports and saves --
    {
        QString why;
        const QString created = db.createAssetEntry(
            "guid-door-create", "made.mat", static_cast<int>(ModelTypes::Material), QString(),
            assethome::assets(), assethome::Origin::Create, QString(), QString(), QByteArray(),
            QByteArray(), QByteArray(), QByteArray(), &why);
        CHECK(created.isEmpty() && db.fetchAsset("guid-door-create").guid.isEmpty(),
              "createAssetEntry REFUSES an Assets row whose origin is a creation");
        CHECK(why.contains("Assets holds only what the user imports or saves"),
              "...and says why in the user's words");
        why.clear();
        const QString noProject = db.createAssetEntry(
            "guid-door-noproj", "x", static_cast<int>(ModelTypes::Object), QString(),
            assethome::project(QString()), assethome::Origin::Create, QString(), QString(),
            QByteArray(), QByteArray(), QByteArray(), QByteArray(), &why);
        CHECK(noProject.isEmpty() && !why.isEmpty(), "...and a project row with no project");
        CHECK(!db.createAssetEntry("guid-door-saved", "saved.mat",
                                   static_cast<int>(ModelTypes::Material), QString(),
                                   assethome::assets(), assethome::Origin::ExplicitSave)
                   .isEmpty(),
              "an explicit save into Assets is accepted");
        CHECK(!db.createAssetEntry("guid-door-mat", "mine.mat",
                                   static_cast<int>(ModelTypes::Material), QString(),
                                   assethome::materials(), assethome::Origin::Create)
                   .isEmpty(),
              "a creation in the Materials storage is accepted");
        const AssetRecord mine = db.fetchAsset("guid-door-mat");
        CHECK(mine.home() == assethome::materials() && mine.projectGuid.isEmpty()
                  && mine.origin == QLatin1String("create"),
              "...and the stored flags are the home's: Materials, no project, origin create");
        CHECK(!db.setAssetHome("guid-door-mat", assethome::assets(), assethome::Origin::Create),
              "setAssetHome refuses to move a creation into Assets");
        CHECK(db.fetchAsset("guid-door-mat").home() == assethome::materials(),
              "...and the row stays where it was");
        bool listed = false;
        for (const AssetRecord &r : db.fetchAssetsForAssetView())
            listed |= r.guid == QLatin1String("guid-door-mat");
        CHECK(!listed, "a Materials-storage row is never an Assets tile");
    }
    CHECK(db.switchAssetCollection(swords, swordGuid), "sword filed under Swords");
    CHECK(db.switchAssetCollection(weapons, shieldGuid), "shield filed under Weaponry");
    CHECK(db.countAssetsInCollections(db.fetchCollectionSubtree(props)) == 2,
          "countAssetsInCollections sees the branch's assets");

    CHECK(!db.deleteCollection(0), "Uncategorized is not deletable");
    CHECK(!db.deleteCollection(-1), "the virtual root is not deletable");
    CHECK(!db.deleteCollection(424242), "unknown drawer delete refused");

    CHECK(db.deleteCollection(props), "delete Props (whole branch)");
    {
        auto colls = db.fetchCollections();
        CHECK(findCollection(colls, props).parent == -2
                  && findCollection(colls, weapons).parent == -2
                  && findCollection(colls, swords).parent == -2, "branch rows removed");
        CHECK(findCollection(colls, scenery).parent == -1, "unrelated drawer untouched");
        CHECK(findCollection(colls, 0).parent == -1, "Uncategorized untouched");
    }
    CHECK(collectionOfAsset(swordGuid) == 0, "sword reassigned to Uncategorized");
    CHECK(collectionOfAsset(shieldGuid) == 0, "shield reassigned to Uncategorized");

    // The orphan-proofing on the grid query: an asset pointing at a dead
    // collection id must still be returned (LEFT JOIN, not INNER).
    {
        testsql::run(db.connection(), QStringLiteral("UPDATE assets SET collection = 987654 WHERE guid = ?"), swordGuid);
        bool seen = false;
        for (const auto &rec : db.fetchAssetsForAssetView())
            if (rec.guid == swordGuid) seen = true;
        CHECK(seen, "asset with orphaned collection still listed (LEFT JOIN)");
        testsql::run(db.connection(), QStringLiteral("UPDATE assets SET collection = 0 WHERE guid = ?"), swordGuid);
    }

    // --- Material-asset data heal (PBR data-loss audit) ---------------------
    db.closeDatabase();
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("all drawers db checks passed\n");
    return 0;
}
