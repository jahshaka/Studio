// Phase-0 suite for the ASSET PIPELINE program (ASSET_PIPELINE_SPEC §3.4
// phase 0 + preflight §1.1/§1.4):
//
//  1. AssetStorePaths unit — THE single path authority: default root, root
//     override (the phase-1 relocatable-root seam and the migration-rehearsal
//     explicit-root seam), legacy per-guid layout, and the phase-2 CAS layout
//     (2-char fan-out, lowercased oids, sidecar/derived/store.json).
//  2. Dependency-export regression — the selectDep bug: export used to walk
//     the WRONG edge direction (dependee = asset) and keep at most ONE row
//     (`if (first())`). Against the real Database on a throwaway SQLite file:
//     an asset with two outgoing dependencies exports BOTH, and a reverse
//     (incoming) edge is NOT exported.
//  3. The phase-0 dependencies indices exist after createAllTables.
//
// Headless (offscreen platform); never touches the user's live JahLibrary.db.
// Framework-free; non-zero exit on failure.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <cstdio>

#include <QLockFile>

#include "data/database/database.h"
#include "services/assetstore.h"
#include "services/assetstorepaths.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static void testPathsAuthority()
{
    printf("--- AssetStorePaths unit ---\n");

    // Default root = AppData + /AssetStore — exactly the historical path.
    AssetStorePaths::setRootOverride(QString());
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    CHECK(AssetStorePaths::defaultRoot() == QDir::cleanPath(appData + "/AssetStore"),
          "defaultRoot is AppData/AssetStore");
    CHECK(AssetStorePaths::root() == AssetStorePaths::defaultRoot(),
          "root() == defaultRoot() with no override");

    // Override (phase-1 setting / rehearsal tools): trailing slash is eaten,
    // all derived paths follow.
    AssetStorePaths::setRootOverride("/tmp/jah-store-test/");
    CHECK(AssetStorePaths::root() == "/tmp/jah-store-test", "override root normalized (no trailing slash)");
    CHECK(AssetStorePaths::legacyFolder("GUID123") == "/tmp/jah-store-test/GUID123",
          "legacyFolder = <root>/<guid>");
    CHECK(AssetStorePaths::legacyFilePath("GUID123", "model.glb") == "/tmp/jah-store-test/GUID123/model.glb",
          "legacyFilePath = <root>/<guid>/<name>");

    // CAS layout (phase 2): 2-char fan-out, lowercase oid + ext.
    const QString oid = "ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
    CHECK(AssetStorePaths::objectPath(oid, "GLB")
              == "/tmp/jah-store-test/objects/ab/abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789.glb",
          "objectPath = <root>/objects/aa/<oid>.<ext>, lowercased");
    CHECK(AssetStorePaths::objectPath(oid.toLower(), QString())
              == "/tmp/jah-store-test/objects/ab/" + oid.toLower(),
          "objectPath without ext has no trailing dot");
    CHECK(AssetStorePaths::sidecarPath("GUID123") == "/tmp/jah-store-test/sidecar/GUID123.json",
          "sidecarPath = <root>/sidecar/<guid>.json");
    CHECK(AssetStorePaths::derivedPath("cachekey42") == "/tmp/jah-store-test/derived/cachekey42",
          "derivedPath = <root>/derived/<key>");
    CHECK(AssetStorePaths::storeInfoPath() == "/tmp/jah-store-test/store.json",
          "storeInfoPath = <root>/store.json");

    // Explicit-root variants (migration rehearsal against a copied library).
    CHECK(AssetStorePaths::legacyFolderIn("/mnt/copy", "G") == "/mnt/copy/G",
          "legacyFolderIn uses the explicit root");
    CHECK(AssetStorePaths::objectPathIn("/mnt/copy", "aabb", "png") == "/mnt/copy/objects/aa/aabb.png",
          "objectPathIn uses the explicit root");
    CHECK(AssetStorePaths::sidecarPathIn("/mnt/copy", "G") == "/mnt/copy/sidecar/G.json",
          "sidecarPathIn uses the explicit root");
    CHECK(AssetStorePaths::storeInfoPathIn("/mnt/copy") == "/mnt/copy/store.json",
          "storeInfoPathIn uses the explicit root");

    // Clearing the override restores the default.
    AssetStorePaths::setRootOverride(QString());
    CHECK(AssetStorePaths::root() == AssetStorePaths::defaultRoot(), "override cleared restores default");
}

static void insertAsset(const QString &guid, int type, const QString &name)
{
    QSqlQuery q;
    q.prepare("INSERT INTO assets (guid, type, name, view_filter) VALUES (?, ?, ?, 2)");
    q.addBindValue(guid);
    q.addBindValue(type);
    q.addBindValue(name);
    q.exec();
}

static void insertDep(const QString &depender, int dependerType,
                      const QString &dependee, int dependeeType, const QString &id)
{
    QSqlQuery q;
    q.prepare("INSERT INTO dependencies (depender_type, dependee_type, project_guid, depender, dependee, id) "
              "VALUES (?, ?, '', ?, ?, ?)");
    q.addBindValue(dependerType);
    q.addBindValue(dependeeType);
    q.addBindValue(depender);
    q.addBindValue(dependee);
    q.addBindValue(id);
    q.exec();
}

static void testDependencyExport(Database &db)
{
    printf("--- dependency-export regression (selectDep direction + all rows) ---\n");

    // Asset A depends on B and C (outgoing edges). D depends on A (incoming —
    // must NOT be exported as one of A's dependencies).
    insertAsset("guidA", 1, "modelA");
    insertAsset("guidB", 2, "texB");
    insertAsset("guidC", 2, "texC");
    insertAsset("guidD", 1, "modelD");
    insertDep("guidA", 1, "guidB", 2, "dep1");
    insertDep("guidA", 1, "guidC", 2, "dep2");
    insertDep("guidD", 1, "guidA", 1, "dep3");

    const QString exportPath = "assetpaths_export_test.db";
    QFile::remove(exportPath);
    CHECK(db.createBlobFromAsset("guidA", exportPath), "createBlobFromAsset succeeded");

    {
        QSqlDatabase check = QSqlDatabase::addDatabase("QSQLITE", "ExportCheckConnection");
        check.setDatabaseName(exportPath);
        CHECK(check.open(), "exported blob DB opens");

        QSqlQuery q(check);

        // The bundle carries A + its two dependencies.
        q.exec("SELECT COUNT(*) FROM assets");
        q.first();
        CHECK(q.value(0).toInt() == 3, "exported assets = A + 2 dependencies (3 rows)");

        // BOTH outgoing dependency rows survive (the old `if (first())`
        // exported at most one; the old WHERE direction exported dep3 instead).
        q.exec("SELECT COUNT(*) FROM dependencies");
        q.first();
        CHECK(q.value(0).toInt() == 2, "exported dependencies = both outgoing rows");

        q.exec("SELECT COUNT(*) FROM dependencies WHERE depender = 'guidA'");
        q.first();
        CHECK(q.value(0).toInt() == 2, "both exported rows have depender = A (outgoing direction)");

        q.exec("SELECT COUNT(*) FROM dependencies WHERE dependee = 'guidA'");
        q.first();
        CHECK(q.value(0).toInt() == 0, "the incoming edge (D depends on A) was not exported");

        check.close();
    }
    QSqlDatabase::removeDatabase("ExportCheckConnection");
    QFile::remove(exportPath);
}

static void testIndices()
{
    printf("--- phase-0 dependencies indices ---\n");
    QSqlQuery q;
    q.exec("SELECT name FROM sqlite_master WHERE type = 'index' AND tbl_name = 'dependencies'");
    QStringList names;
    while (q.next()) names << q.value(0).toString();
    CHECK(names.contains("idx_dependencies_depender"), "idx_dependencies_depender exists");
    CHECK(names.contains("idx_dependencies_dependee"), "idx_dependencies_dependee exists");
}

static void testLibraryLock()
{
    printf("--- LibraryLock (phase 1) ---\n");
    const QString dbPath = QDir::current().filePath("assetpaths_lock_test.db");
    QFile::remove(dbPath + ".lock");

    CHECK(LibraryLock::acquire(dbPath), "lock acquired beside the db");
    CHECK(!LibraryLock::heldElsewhere(dbPath), "our own lock is not 'elsewhere'");

    QLockFile probe(dbPath + ".lock");
    probe.setStaleLockTime(0);
    CHECK(!probe.tryLock(0), "a second locker is refused while we hold it");

    LibraryLock::release();
    CHECK(probe.tryLock(0), "released lock is acquirable again");
    // While the probe holds it, it IS held elsewhere (another QLockFile).
    CHECK(LibraryLock::heldElsewhere(dbPath), "foreign lock reported heldElsewhere");
    probe.unlock();
    QFile::remove(dbPath + ".lock");
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets (QMessageBox)

    testPathsAuthority();
    testLibraryLock();

    const QString dbPath = "assetpaths_test.db";
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    testIndices();
    testDependencyExport(db);

    db.closeDatabase();
    QFile::remove(dbPath);

    printf(failures ? "FAILED: %d check(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
