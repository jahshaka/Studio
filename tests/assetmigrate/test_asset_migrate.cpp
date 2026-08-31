// Phase-2 suite for the ASSET PIPELINE program (ASSET_PIPELINE_SPEC §3.1.3 +
// preflight amendments): the content-addressed store on a FIXTURE library.
//
//   - migrateStore: hashes every library file (view_filter 2 AND 3 — an
//     Effects row is included; Editor rows are skipped), 2-char fan-out
//     objects, files/asset_files rows with refcounts, sidecars, store.json;
//     the legacy tree is RETAINED; a row with no folder is zero files;
//   - dedup: identical content under two names/extensions = ONE object;
//   - resolver: asset_files → objects path, byte-identical; legacy fallback;
//   - verify: clean store, then detected bit-rot on a corrupted object;
//   - idempotency: second migrate run creates zero new objects, same rows;
//   - rebuildCatalog: fresh DB reconstructed from sidecars matches;
//   - refusal: migration refuses while another process holds the lock.
//
// Headless (offscreen platform); throwaway files in the test working dir.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <cstdio>

#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/assetmigration.h"
#include "services/assetstorepaths.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(bytes);
}

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    return file.readAll();
}

static void insertAsset(const QString &guid, int type, const QString &name, int viewFilter)
{
    QSqlQuery q;
    q.prepare("INSERT INTO assets (guid, type, name, view_filter, collection, author, license, properties) "
              "VALUES (?, ?, ?, ?, 0, 'tester', 'MIT', '{\"metadata\":{\"kind\":\"test\"}}')");
    q.addBindValue(guid);
    q.addBindValue(type);
    q.addBindValue(name);
    q.addBindValue(viewFilter);
    q.exec();
}

static int countRows(QSqlDatabase conn, const QString &sql)
{
    QSqlQuery q(conn);
    q.exec(sql);
    return q.next() ? q.value(0).toInt() : -1;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets

    const QString cwd = QDir::currentPath();
    const QString dbPath = cwd + "/migrate_test.db";
    const QString root = cwd + "/migrate_store_root";
    QFile::remove(dbPath);
    QFile::remove(dbPath + ".lock");
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    const QByteArray contentX(1500, 'X');
    const QByteArray contentY = QByteArray("PNGISH-").repeated(200);
    const QString oidX = AssetCas::hashFile(QString());   // exercise the error path
    CHECK(oidX.isEmpty(), "hashFile of a missing path returns empty");

    // ---- fixture library ----
    Database db;
    CHECK(db.initializeDatabase(dbPath), "fixture database opened");
    db.createAllTables();

    insertAsset("guidA", 1, "a.glb", 2);            // AssetsView row with one file
    insertAsset("guidB", 7, "b.png", 3);            // EFFECTS row (preflight §1.6) with two files
    insertAsset("guidC", 1, "ghost.glb", 2);        // library row with NO folder
    insertAsset("guidD", 1, "editor.glb", 1);       // Editor row — must be SKIPPED
    writeFile(root + "/guidA/a.glb", contentX);
    writeFile(root + "/guidB/b.png", contentY);
    writeFile(root + "/guidB/dup.bin", contentX);   // same bytes as a.glb — dedup
    writeFile(root + "/guidD/editor.glb", QByteArray(64, 'D'));
    db.closeDatabase();

    // ---- refusal while a foreign lock is held ----
    {
        QLockFile foreign(dbPath + ".lock");
        foreign.setStaleLockTime(0);
        CHECK(foreign.tryLock(0), "test holds a foreign lock");
        const auto refused = AssetMigration::migrateStore(dbPath, root);
        CHECK(!refused.ok && refused.error.contains("another"),
              "migrateStore refuses while the library is locked elsewhere");
        foreign.unlock();
    }

    // ---- the migration ----
    const auto report = AssetMigration::migrateStore(dbPath, root);
    CHECK(report.ok, "migrateStore succeeded");
    CHECK(report.libraryRows == 3, "3 library rows scanned (Editor row skipped)");
    CHECK(report.rowsWithFiles == 2, "2 rows had legacy folders");
    CHECK(report.rowsWithoutFiles == 1, "1 row without a folder = zero files, not an error");
    CHECK(report.filesSeen == 3, "3 files hashed");
    CHECK(report.objectsCreated == 2, "2 distinct objects created (dedup)");
    CHECK(report.objectsReused == 1, "1 duplicate content reused");
    CHECK(report.sidecars == 3, "3 sidecars written");

    const QString hashX = AssetCas::hashFile(root + "/guidA/a.glb");
    const QString hashY = AssetCas::hashFile(root + "/guidB/b.png");
    CHECK(hashX.size() == 64 && hashX == hashX.toLower(), "sha256 oid is 64 lowercase hex chars");
    CHECK(QFileInfo::exists(AssetStorePaths::objectPathIn(root, hashX, "glb")),
          "object for X at objects/<aa>/<oid>.glb");
    CHECK(QFileInfo::exists(AssetStorePaths::objectPathIn(root, hashY, "png")),
          "object for Y at objects/<aa>/<oid>.png");
    CHECK(readFile(AssetStorePaths::objectPathIn(root, hashX, "glb")) == contentX,
          "object bytes are identical to the source");
    CHECK(QFileInfo::exists(root + "/guidA/a.glb"), "legacy tree RETAINED after migration");
    CHECK(QFileInfo::exists(AssetStorePaths::storeInfoPathIn(root)), "store.json written");
    CHECK(QFileInfo::exists(AssetStorePaths::sidecarPathIn(root, "guidC")),
          "file-less library row still gets a sidecar");
    CHECK(!QFileInfo::exists(AssetStorePaths::sidecarPathIn(root, "guidD")),
          "Editor row got NO sidecar (skipped)");

    // ---- catalog rows on a checking connection ----
    {
        QSqlDatabase check = QSqlDatabase::addDatabase("QSQLITE", "MigrateCheck");
        check.setDatabaseName(dbPath);
        check.open();
        CHECK(countRows(check, "SELECT COUNT(*) FROM files") == 2, "2 files rows");
        CHECK(countRows(check, "SELECT COUNT(*) FROM asset_files") == 3, "3 asset_files rows");
        CHECK(countRows(check, "SELECT refcount FROM files WHERE oid = '" + hashX + "'") == 2,
              "dedup'd object has refcount 2 (trigger-maintained)");
        CHECK(countRows(check, "SELECT COUNT(*) FROM asset_files WHERE asset_guid = 'guidD'") == 0,
              "Editor row has no asset_files rows");
        QSqlQuery version(check);
        version.exec("PRAGMA user_version");
        version.next();
        CHECK(version.value(0).toInt() >= 1, "PRAGMA user_version set");

        // resolver: asset_files → object path with identical bytes; unknown
        // name falls back to the legacy folder.
        const QString resolved = AssetCas::resolveFile(check, root, "guidB", "dup.bin");
        CHECK(!resolved.isEmpty() && readFile(resolved) == contentX,
              "resolver returns byte-identical content for a dedup'd name");
        writeFile(root + "/guidB/legacy_only.txt", "legacy");
        CHECK(AssetCas::resolveFile(check, root, "guidB", "legacy_only.txt")
                  == root + "/guidB/legacy_only.txt",
              "resolver falls back to the legacy folder for unmigrated files");
        QFile::remove(root + "/guidB/legacy_only.txt");
        check.close();
    }
    QSqlDatabase::removeDatabase("MigrateCheck");

    // ---- verify: clean, then corrupted ----
    auto verifyReport = AssetMigration::verify(dbPath, root);
    CHECK(verifyReport.ok, "verify: clean store");
    CHECK(verifyReport.objects == 2, "verify walked both objects");

    const QString objectY = AssetStorePaths::objectPathIn(root, hashY, "png");
    {
        // Break the hardlink first so the corruption can't reach back into
        // the legacy source file.
        const QByteArray original = readFile(objectY);
        QFile::remove(objectY);
        writeFile(objectY, original + "CORRUPTED");
    }
    verifyReport = AssetMigration::verify(dbPath, root);
    CHECK(!verifyReport.ok && verifyReport.corrupt.size() == 1 && verifyReport.corrupt[0] == hashY,
          "verify detects the corrupted object");
    {
        QFile::remove(objectY);
        writeFile(objectY, contentY);   // restore
    }
    CHECK(AssetMigration::verify(dbPath, root).ok, "verify clean again after restore");

    // ---- idempotency: second run, nothing new ----
    const auto rerun = AssetMigration::migrateStore(dbPath, root);
    CHECK(rerun.ok, "second migrateStore run succeeded");
    CHECK(rerun.objectsCreated == 0, "idempotent: zero new objects on rerun");
    CHECK(rerun.objectsReused == 3, "idempotent: every file reuses its object");
    {
        QSqlDatabase check = QSqlDatabase::addDatabase("QSQLITE", "MigrateCheck2");
        check.setDatabaseName(dbPath);
        check.open();
        CHECK(countRows(check, "SELECT COUNT(*) FROM asset_files") == 3, "idempotent: still 3 asset_files rows");
        CHECK(countRows(check, "SELECT refcount FROM files WHERE oid = '" + hashX + "'") == 2,
              "idempotent: refcount unchanged on rerun");
        check.close();
    }
    QSqlDatabase::removeDatabase("MigrateCheck2");

    // ---- rebuildCatalog into a FRESH database (the honest I2 test) ----
    const QString rebuiltDb = cwd + "/migrate_rebuilt.db";
    QFile::remove(rebuiltDb);
    const auto rebuild = AssetMigration::rebuildCatalog(rebuiltDb, root);
    CHECK(rebuild.ok, "rebuildCatalog succeeded");
    CHECK(rebuild.assets == 3, "rebuild recovered 3 asset rows");
    CHECK(rebuild.files == 2, "rebuild recovered 2 files rows");
    CHECK(rebuild.links == 3, "rebuild recovered 3 asset_files rows");
    {
        QSqlDatabase check = QSqlDatabase::addDatabase("QSQLITE", "RebuildCheck");
        check.setDatabaseName(rebuiltDb);
        check.open();
        QSqlQuery q(check);
        q.exec("SELECT name, type, view_filter FROM assets WHERE guid = 'guidB'");
        CHECK(q.next() && q.value(0).toString() == "b.png" && q.value(1).toInt() == 7
                  && q.value(2).toInt() == 3,
              "rebuilt Effects row matches (name/type/view_filter)");
        CHECK(countRows(check, "SELECT refcount FROM files WHERE oid = '" + hashX + "'") == 2,
              "rebuilt refcounts are trigger-consistent");
        check.close();
    }
    QSqlDatabase::removeDatabase("RebuildCheck");

    // ---- cleanup ----
    QFile::remove(dbPath);
    QFile::remove(dbPath + ".lock");
    QFile::remove(rebuiltDb);
    QDir(root).removeRecursively();

    printf(failures ? "FAILED: %d check(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
