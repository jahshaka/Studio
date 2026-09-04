// assets.gc — THE ADVERSARIAL SUITE (deep audit 2026-09, area 6).
//
// The collector's one law is that it must never delete live data, so this
// suite is built the other way round from a normal one: the fixture store is
// seeded with every class of garbage AND with a LOOK-ALIKE for each — an
// artifact that presents exactly like that garbage and is live:
//
//   garbage                          live look-alike
//   ------------------------------   --------------------------------------
//   files row nothing references     a copy-on-write object with refcount 0
//                                    whose ONLY reference is a project pin
//   files row nothing references     an object whose refcount CACHE says 0
//                                    while an asset_files row names it
//   object with no files row         an object shared by two assets
//   abandoned staging temp           a staging temp written seconds ago
//   sidecar naming no asset          a sidecar naming a live asset
//   per-guid folder naming no asset  a per-guid folder of a live asset
//                                    holding bytes the CAS does NOT have
//
// Then: verify() green, a dry run that reports EXACTLY the garbage and
// touches nothing, a real run that removes EXACTLY the garbage, every live
// artifact re-read and compared BYTE FOR BYTE, verify() green again, a second
// sweep that finds nothing, and the refusal that protects a store this
// catalog does not recognize.
//
// Plus the sidecar lifecycle the GC's sweep (c) depends on (invariant I2):
// a mutation refreshes the sidecar, a delete removes it, and rebuildCatalog
// skips tombstones instead of resurrecting deleted assets.
//
// Headless (offscreen platform); throwaway files in the test working dir.
// The store root override is set for the WHOLE run — nothing here may ever
// address the developer's real library.
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <cstdio>

#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/assetgc.h"
#include "services/assetmigration.h"
#include "services/assetstorepaths.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    file.write(bytes);
}

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    return file.readAll();
}

static void insertAsset(const QString &guid, int type, const QString &name)
{
    QSqlQuery q;
    q.prepare("INSERT INTO assets (guid, type, name, view_filter, collection, author, license, properties) "
              "VALUES (?, ?, ?, 2, 0, 'tester', 'MIT', '{}')");
    q.addBindValue(guid);
    q.addBindValue(type);
    q.addBindValue(name);
    q.exec();
}

static int scalar(QSqlDatabase conn, const QString &sql)
{
    QSqlQuery q(conn);
    if (!q.exec(sql) || !q.next()) return -1;
    return q.value(0).toInt();
}

// Does a report class name this path?
static bool reports(const AssetGc::ClassReport &cls, const QString &path)
{
    for (const AssetGc::Item &item : cls.items)
        if (QDir::cleanPath(item.path) == QDir::cleanPath(path)) return true;
    return false;
}

static bool reportsId(const AssetGc::ClassReport &cls, const QString &id)
{
    for (const AssetGc::Item &item : cls.items) if (item.id == id) return true;
    return false;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets

    const QString cwd = QDir::currentPath();
    const QString dbPath = cwd + "/gc_test.db";
    const QString root   = cwd + "/gc_store_root";
    const QString srcDir = cwd + "/gc_sources";
    QFile::remove(dbPath);
    QFile::remove(dbPath + ".lock");
    QDir(root).removeRecursively();
    QDir(srcDir).removeRecursively();
    QDir().mkpath(root);

    // THE ROOT OVERRIDE COVERS THE WHOLE RUN: Database's sidecar hooks read
    // AssetStorePaths::root(), which otherwise points at the developer's real
    // AppData store.
    AssetStorePaths::setRootOverride(root);

    const QByteArray bytesShared  = QByteArray("SHARED--").repeated(200);
    const QByteArray bytesSolo    = QByteArray("SOLO----").repeated(150);
    const QByteArray bytesCowBase = QByteArray("COWBASE-").repeated(120);
    const QByteArray bytesCowEdit = QByteArray("COWEDIT-").repeated(180);
    const QByteArray bytesDrift   = QByteArray("DRIFT---").repeated(90);
    const QByteArray bytesDead    = QByteArray("DEADOBJ-").repeated(70);
    const QByteArray bytesStray   = QByteArray("STRAY---").repeated(60);
    const QByteArray bytesLegacy  = QByteArray("LEGACY--").repeated(50);

    writeFile(srcDir + "/shared.bin",   bytesShared);
    writeFile(srcDir + "/solo.bin",     bytesSolo);
    writeFile(srcDir + "/cowbase.glb",  bytesCowBase);
    writeFile(srcDir + "/cowedit.glb",  bytesCowEdit);
    writeFile(srcDir + "/drift.bin",    bytesDrift);
    writeFile(srcDir + "/dead.bin",     bytesDead);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "fixture database opened");
    db.createAllTables();
    QSqlDatabase conn = QSqlDatabase::database();

    // ================= the LIVE half =================
    insertAsset("guidShared", 1, "shared.bin");
    insertAsset("guidOther",  1, "shared.bin");   // same content, second asset
    insertAsset("guidSolo",   1, "solo.bin");
    insertAsset("guidCow",    1, "cowbase.glb");
    insertAsset("guidDrift",  1, "drift.bin");
    insertAsset("guidLegacyLive", 1, "keepme.bin");

    QString error;
    QString oidShared, oidSolo, oidCowBase, oidCowEdit, oidDrift, oidDead;
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/shared.bin", "guidShared", "source", "shared.bin", &oidShared, &error),
          "ingest the shared object under guidShared");
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/shared.bin", "guidOther", "source", "shared.bin", nullptr, &error),
          "the SAME content under a second asset (dedup, refcount 2)");
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/solo.bin", "guidSolo", "source", "solo.bin", &oidSolo, &error),
          "ingest a plain single-reference object");
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/cowbase.glb", "guidCow", "source", "cowbase.glb", &oidCowBase, &error),
          "ingest the copy-on-write asset's ORIGINAL bytes");
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/drift.bin", "guidDrift", "source", "drift.bin", &oidDrift, &error),
          "ingest the refcount-drift asset");

    // THE COPY-ON-WRITE LOOK-ALIKE. The edited bytes ingest under the SAME
    // (guid, role, name) key, so the asset_files INSERT OR IGNORE does
    // nothing: the new object's refcount is 0 and its only reference in the
    // entire catalog is the project's pin. A refcount-only GC eats it.
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/cowedit.glb", "guidCow", "source", "cowbase.glb", &oidCowEdit, &error),
          "copy-on-write ingest of the edited bytes");
    CHECK(oidCowEdit != oidCowBase, "the edit is a different object");
    CHECK(AssetCas::writePin(conn, "projP", "guidCow", oidCowEdit), "the project pins the edited bytes");
    CHECK(scalar(conn, "SELECT refcount FROM files WHERE oid = '" + oidCowEdit + "'") == 0,
          "the pinned copy-on-write object really does carry refcount 0");
    CHECK(scalar(conn, "SELECT COUNT(*) FROM asset_files WHERE oid = '" + oidCowEdit + "'") == 0,
          "…and really has no asset_files row (the pin is its ONLY reference)");

    // THE REFCOUNT-DRIFT LOOK-ALIKE: an object with a live asset_files row
    // whose cached counter has been corrupted to 0.
    {
        QSqlQuery q(conn);
        q.exec("UPDATE files SET refcount = 0 WHERE oid = '" + oidDrift + "'");
    }
    CHECK(scalar(conn, "SELECT COUNT(*) FROM asset_files WHERE oid = '" + oidDrift + "'") == 1,
          "the drift object is still named by an asset_files row");

    // THE MESH BAKE (MESH_BAKE_SPEC phase 1): derived data recorded as an
    // ordinary asset_files row with role 'bake'. It must be reachable through
    // that row like any other file — the GC knows nothing about bakes, and the
    // point of this case is to prove it does not need to. It must survive the
    // sweep, and it must be collected when its asset dies.
    const QByteArray bytesBake = QByteArray("MESHBAKE").repeated(80);
    writeFile(srcDir + "/model.jmb", bytesBake);
    QString oidBake;
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/model.jmb", "guidCow", "bake",
                               "model.jmb", &oidBake, &error),
          "a role-'bake' derived file recorded against a live asset");
    CHECK(scalar(conn, "SELECT COUNT(*) FROM asset_files WHERE oid = '" + oidBake + "' AND role = 'bake'") == 1,
          "the bake has exactly one asset_files row, under its asset");

    // A live sidecar, and a live per-guid folder holding bytes the CAS does
    // NOT have (the materials-module texture shape).
    CHECK(AssetCas::writeSidecar(conn, root, "guidSolo", &error), "sidecar for a live asset");
    writeFile(root + "/guidLegacyLive/keepme.bin", bytesLegacy);

    // A redundant legacy-view entry for a LIVE asset: byte-for-byte the CAS
    // object. This one IS reclaimable — it is the second copy the retired
    // view used to cost.
    writeFile(root + "/guidShared/shared.bin", bytesShared);

    // A staging temp written just now: it may belong to an import in flight.
    const QString freshTemp = AssetStorePaths::objectPathIn(root, oidSolo, "bin") + ".tmp-4242-0";
    writeFile(freshTemp, bytesSolo);

    // ================= the GARBAGE half =================
    // (a1) a files row + object nothing references
    CHECK(AssetCas::ingestFile(conn, root, srcDir + "/dead.bin", "guidDead", "source", "dead.bin", &oidDead, &error),
          "ingest an object under an asset that will not exist");
    {
        QSqlQuery q(conn);   // drop the mapping, keep the row + the object
        q.exec("DELETE FROM asset_files WHERE asset_guid = 'guidDead'");
    }
    const QString deadObject = AssetStorePaths::objectPathIn(root, oidDead, "bin");
    CHECK(QFileInfo::exists(deadObject), "the unreferenced object is on disk");

    // (a2) an ORPHAN CATALOG ROW: a files row whose object was never written
    const QString ghostOid = QString(64, QLatin1Char('a'));
    {
        QSqlQuery q(conn);
        q.prepare("INSERT INTO files (oid, size, ext, refcount) VALUES (?, 4096, 'bin', 0)");
        q.addBindValue(ghostOid);
        q.exec();
    }

    // (b1) an object on disk the catalog never recorded
    const QString strayOid = QString(64, QLatin1Char('b'));
    const QString strayObject = AssetStorePaths::objectPathIn(root, strayOid, "bin");
    writeFile(strayObject, bytesStray);

    // (b2) an ABANDONED staging temp (backdated two hours)
    const QString staleTemp = AssetStorePaths::objectPathIn(root, oidShared, "bin") + ".tmp-999-9";
    writeFile(staleTemp, bytesStray);
    {
        // setFileTime needs the file OPEN (it is a QFileDevice operation on
        // the handle) — a closed QFile silently does nothing.
        QFile f(staleTemp);
        CHECK(f.open(QIODevice::ReadWrite), "opened the staging temp to backdate it");
        CHECK(f.setFileTime(QDateTime::currentDateTime().addSecs(-7200),
                            QFileDevice::FileModificationTime),
              "backdated the abandoned staging temp by two hours");
        f.close();
    }

    // (c) a sidecar naming no asset row (the tombstone class: 41 of the
    //     owner's 86 sidecars were exactly this)
    const QString deadSidecar = AssetStorePaths::sidecarPathIn(root, "guidGhostAsset");
    writeFile(deadSidecar, QByteArray(
        "{\"formatVersion\":1,\"guid\":\"guidGhostAsset\",\"name\":\"ghost.bin\",\"type\":1,"
        "\"files\":[{\"role\":\"source\",\"oid\":\"" + ghostOid.toUtf8() + "\",\"name\":\"ghost.bin\",\"size\":4096,\"ext\":\"bin\"}]}"));

    // (d) a per-guid folder naming no asset row
    const QString deadFolder = root + "/guidGhostFolder";
    writeFile(deadFolder + "/leftover.bin", bytesStray);

    // ---- verify() is green BEFORE the sweep (bar the ghost row we planted) ----
    {
        conn = QSqlDatabase();
        db.closeDatabase();
        const auto before = AssetMigration::verify(dbPath, root);
        CHECK(!before.ok && before.missing.size() == 1 && before.missing[0] == ghostOid,
              "verify sees exactly the planted orphan catalog row, nothing else");
        CHECK(before.corrupt.isEmpty(), "verify finds no corruption before the sweep");
        CHECK(db.initializeDatabase(dbPath), "database reopened");
        conn = QSqlDatabase::database();
    }

    // ================= THE DRY RUN =================
    const auto dry = AssetGc::sweep(conn, root, /*dryRun*/ true);
    CHECK(dry.ok, "dry run succeeded");
    CHECK(dry.dryRun, "the report says it was a dry run");
    CHECK(dry.removedCount() == 0, "a dry run removes nothing");

    CHECK(dry.unreferencedObjects.items.size() == 2,
          "dry run: exactly 2 unreferenced catalog objects (the dropped mapping + the ghost row)");
    CHECK(reportsId(dry.unreferencedObjects, oidDead), "dry run names the unreferenced object");
    CHECK(reportsId(dry.unreferencedObjects, ghostOid), "dry run names the orphan catalog row");
    CHECK(!reportsId(dry.unreferencedObjects, oidCowEdit),
          "THE LAW: the pinned copy-on-write object (refcount 0) is NOT collected");
    CHECK(!reportsId(dry.unreferencedObjects, oidDrift),
          "THE LAW: an object whose refcount CACHE reads 0 but whose row exists is NOT collected");
    CHECK(!reportsId(dry.unreferencedObjects, oidBake),
          "THE LAW: a role-'bake' file is reachable through its asset row and is NOT collected");
    CHECK(!reports(dry.strayObjects, AssetStorePaths::objectPathIn(root, oidBake, "jmb")),
          "…and the bake object is not mistaken for an uncatalogued stray");
    CHECK(!reportsId(dry.unreferencedObjects, oidShared), "the shared object is not collected");
    CHECK(!reportsId(dry.unreferencedObjects, oidSolo), "the single-reference object is not collected");
    CHECK(dry.refcountDrift >= 1, "the drifted refcount is REPORTED (never acted on)");

    CHECK(dry.strayObjects.items.size() == 2, "dry run: exactly 2 stray files under objects/");
    CHECK(reports(dry.strayObjects, strayObject), "dry run names the uncatalogued object");
    CHECK(reports(dry.strayObjects, staleTemp), "dry run names the abandoned staging temp");
    CHECK(!reports(dry.strayObjects, freshTemp), "THE LAW: a staging temp written just now is left alone");

    CHECK(dry.straySidecars.items.size() == 1, "dry run: exactly 1 stray sidecar");
    CHECK(reports(dry.straySidecars, deadSidecar), "dry run names the tombstone sidecar");
    CHECK(!reports(dry.straySidecars, AssetStorePaths::sidecarPathIn(root, "guidSolo")),
          "THE LAW: the live asset's sidecar is not collected");

    CHECK(dry.legacyFolders.items.size() == 1, "dry run: exactly 1 dead per-guid folder");
    CHECK(reports(dry.legacyFolders, deadFolder), "dry run names the dead folder");
    CHECK(!reports(dry.legacyFolders, root + "/guidLegacyLive"),
          "THE LAW: a live asset's folder is not collected");
    CHECK(!reports(dry.legacyFolders, root + "/guidShared"),
          "THE LAW: a live asset's folder is not collected even when its content IS in the CAS");

    CHECK(dry.redundantLegacyFiles.items.size() == 1, "dry run: exactly 1 redundant legacy copy");
    CHECK(reports(dry.redundantLegacyFiles, root + "/guidShared/shared.bin"),
          "dry run names the duplicated view entry");
    CHECK(!reports(dry.redundantLegacyFiles, root + "/guidLegacyLive/keepme.bin"),
          "THE LAW: a legacy file the CAS does NOT hold is not collected");

    // And nothing moved.
    CHECK(QFileInfo::exists(deadObject) && QFileInfo::exists(strayObject)
              && QFileInfo::exists(deadSidecar) && QDir(deadFolder).exists(),
          "the dry run left every reported artifact in place");

    // ================= THE REFUSAL =================
    {
        // A FRESH catalog over this populated store: every artifact looks
        // unreferenced. The collector must refuse, not reap.
        const QString emptyDbPath = cwd + "/gc_empty.db";
        QFile::remove(emptyDbPath);
        QSqlDatabase empty = QSqlDatabase::addDatabase("QSQLITE", "GcEmpty");
        empty.setDatabaseName(emptyDbPath);
        empty.open();
        AssetCas::ensureCasSchema(empty);
        QSqlQuery(QStringLiteral("CREATE TABLE IF NOT EXISTS assets (guid TEXT PRIMARY KEY, name TEXT)"), empty);

        const auto refused = AssetGc::sweep(empty, root, /*dryRun*/ false);
        CHECK(!refused.ok && !refused.error.isEmpty(),
              "THE LAW: a catalog that knows nothing about a populated store is REFUSED");
        CHECK(refused.totalCount() == 0, "the refusal collected nothing at all");
        CHECK(QFileInfo::exists(AssetStorePaths::objectPathIn(root, oidShared, "bin")),
              "…and the store is untouched by the refusal");

        const auto forced = AssetGc::sweep(empty, root, /*dryRun*/ true, /*force*/ true);
        CHECK(forced.ok && forced.totalCount() > 0, "force overrides the refusal (dry run here)");

        empty.close();
        empty = QSqlDatabase();
        QSqlDatabase::removeDatabase("GcEmpty");
        QFile::remove(emptyDbPath);
    }

    // ================= THE REAL RUN =================
    const int filesBefore = scalar(conn, "SELECT COUNT(*) FROM files");
    const auto run = AssetGc::sweep(conn, root, /*dryRun*/ false);
    CHECK(run.ok && run.failures.isEmpty(), "the real run succeeded with no failures");
    CHECK(!run.dryRun, "the report says it was a real run");
    CHECK(run.removedCount() == dry.totalCount(),
          "the real run removed exactly what the dry run promised");

    CHECK(!QFileInfo::exists(deadObject), "the unreferenced object is gone");
    CHECK(!QFileInfo::exists(strayObject), "the uncatalogued object is gone");
    CHECK(!QFileInfo::exists(staleTemp), "the abandoned staging temp is gone");
    CHECK(!QFileInfo::exists(deadSidecar), "the tombstone sidecar is gone");
    CHECK(!QDir(deadFolder).exists(), "the dead per-guid folder is gone");
    CHECK(!QFileInfo::exists(root + "/guidShared/shared.bin"), "the duplicated view entry is gone");
    CHECK(!QDir(root + "/guidShared").exists(), "…and the folder it emptied went with it");
    CHECK(scalar(conn, "SELECT COUNT(*) FROM files") == filesBefore - 2,
          "exactly the two unreferenced files rows were dropped");

    // ---- EVERY LIVE ARTIFACT, BYTE FOR BYTE ----
    CHECK(readFile(AssetStorePaths::objectPathIn(root, oidShared, "bin")) == bytesShared,
          "LIVE: the shared object is byte-for-byte intact");
    CHECK(readFile(AssetStorePaths::objectPathIn(root, oidSolo, "bin")) == bytesSolo,
          "LIVE: the single-reference object is byte-for-byte intact");
    CHECK(readFile(AssetStorePaths::objectPathIn(root, oidCowBase, "glb")) == bytesCowBase,
          "LIVE: the copy-on-write base object is byte-for-byte intact");
    CHECK(readFile(AssetStorePaths::objectPathIn(root, oidCowEdit, "glb")) == bytesCowEdit,
          "LIVE: THE PINNED COPY-ON-WRITE EDIT is byte-for-byte intact");
    CHECK(readFile(AssetCas::resolvePinned(conn, root, "projP", "guidCow")) == bytesCowEdit,
          "LIVE: the project still resolves its pin to those exact bytes");
    CHECK(readFile(AssetStorePaths::objectPathIn(root, oidDrift, "bin")) == bytesDrift,
          "LIVE: the refcount-drift object is byte-for-byte intact");
    CHECK(readFile(AssetStorePaths::objectPathIn(root, oidBake, "jmb")) == bytesBake,
          "LIVE: the mesh bake survived the sweep byte-for-byte");
    CHECK(readFile(root + "/guidLegacyLive/keepme.bin") == bytesLegacy,
          "LIVE: the legacy-only file is byte-for-byte intact");
    CHECK(readFile(freshTemp) == bytesSolo, "LIVE: the in-flight staging temp is untouched");
    CHECK(QFileInfo::exists(AssetStorePaths::sidecarPathIn(root, "guidSolo")),
          "LIVE: the live asset's sidecar survived");
    CHECK(scalar(conn, "SELECT COUNT(*) FROM assets") == 6, "LIVE: every asset row survived");
    CHECK(scalar(conn, "SELECT COUNT(*) FROM project_assets") == 1, "LIVE: the project pin survived");

    // ---- verify() green again, and the sweep is idempotent ----
    {
        // The fresh temp would be collected by a LATER run once it ages; drop
        // it here so the idempotency check is about the sweep, not the clock.
        QFile::remove(freshTemp);
        const auto again = AssetGc::sweep(conn, root, /*dryRun*/ true);
        CHECK(again.ok && again.totalCount() == 0, "a second sweep finds nothing left to do");
    }
    {
        conn = QSqlDatabase();
        db.closeDatabase();
        const auto after = AssetMigration::verify(dbPath, root);
        CHECK(after.ok, "verify is GREEN after the sweep (the orphan row went with its class)");
        CHECK(db.initializeDatabase(dbPath), "database reopened for the sidecar checks");
        conn = QSqlDatabase::database();
    }

    // ================= SIDECAR LIFECYCLE (invariant I2) =================
    {
        const QString path = AssetStorePaths::sidecarPathIn(root, "guidSolo");
        const auto nameIn = [&]() {
            return QJsonDocument::fromJson(readFile(path)).object().value("name").toString();
        };
        CHECK(nameIn() == "solo.bin", "the sidecar starts out matching the row");

        CHECK(db.renameAsset("guidSolo", "renamed.bin"), "renameAsset");
        CHECK(nameIn() == "renamed.bin", "renameAsset refreshed the sidecar");

        CHECK(db.updateAssetMetadata("guidSolo", "tagged.bin", "{\"tags\":[]}"), "updateAssetMetadata");
        CHECK(nameIn() == "tagged.bin", "updateAssetMetadata refreshed the sidecar");

        CHECK(db.updateAssetProperties("guidSolo", "{\"metadata\":{\"kind\":\"test\"}}"),
              "updateAssetProperties (the lazy metadata backfill's path)");
        CHECK(QJsonDocument::fromJson(readFile(path)).object().value("properties").toObject()
                  .value("metadata").toObject().value("kind").toString() == "test",
              "updateAssetProperties refreshed the sidecar's properties");

        CHECK(db.updateAssetViewFilter("guidSolo", 3), "updateAssetViewFilter");
        CHECK(QJsonDocument::fromJson(readFile(path)).object().value("viewFilter").toInt() == 3,
              "updateAssetViewFilter refreshed the sidecar");

        CHECK(db.switchAssetCollection(7, "guidSolo"), "switchAssetCollection");
        CHECK(QJsonDocument::fromJson(readFile(path)).object().value("collection").toInt() == 7,
              "switchAssetCollection refreshed the sidecar");

        // …and the delete takes it with the asset (the 41-of-86 defect).
        CHECK(db.deleteAsset("guidSolo"), "deleteAsset");
        CHECK(!QFileInfo::exists(path), "deleteAsset removed the sidecar");
        CHECK(scalar(QSqlDatabase::database(), "SELECT COUNT(*) FROM assets WHERE guid = 'guidSolo'") == 0,
              "…and the row really went");
    }

    // ============ THE BAKE DIES WITH ITS ASSET (MESH_BAKE_SPEC) ============
    //
    // Derived data must not outlive what it was derived from. deleteAsset
    // drops every asset_files row, which leaves the bake object unreferenced —
    // and the very next sweep collects it, with no bake-specific code
    // anywhere in the GC.
    {
        QSqlDatabase live = QSqlDatabase::database();
        const QString bakeObject = AssetStorePaths::objectPathIn(root, oidBake, "jmb");
        CHECK(QFileInfo::exists(bakeObject), "the bake is still on disk before its asset dies");
        CHECK(db.deleteAsset("guidCow"), "the asset that owns the bake is deleted");
        CHECK(scalar(live, "SELECT COUNT(*) FROM asset_files WHERE oid = '" + oidBake + "'") == 0,
              "the delete dropped the bake's asset_files row");

        const auto reap = AssetGc::sweep(live, root, /*dryRun*/ true);
        CHECK(reap.ok && reportsId(reap.unreferencedObjects, oidBake),
              "the now-unreferenced bake is reported for collection");
        const auto reaped = AssetGc::sweep(live, root, /*dryRun*/ false);
        CHECK(reaped.ok, "the sweep that reaps the bake succeeded");
        CHECK(!QFileInfo::exists(bakeObject), "the bake object is gone with its asset");
    }

    // ================= rebuildCatalog SKIPS TOMBSTONES =================
    {
        // Two sidecars: one whose object is present, one (hand-written) whose
        // recorded object is not — the shape an old store carries by the
        // dozen. Plus a FILE-LESS sidecar, which is recoverable and must NOT
        // be mistaken for a tombstone.
        conn = QSqlDatabase();
        db.closeDatabase();

        writeFile(AssetStorePaths::sidecarPathIn(root, "guidTombstone"), QByteArray(
            "{\"formatVersion\":1,\"guid\":\"guidTombstone\",\"name\":\"gone.bin\",\"type\":1,"
            "\"files\":[{\"role\":\"source\",\"oid\":\"" + QString(64, QLatin1Char('c')).toUtf8()
            + "\",\"name\":\"gone.bin\",\"size\":10,\"ext\":\"bin\"}]}"));
        writeFile(AssetStorePaths::sidecarPathIn(root, "guidFileless"), QByteArray(
            "{\"formatVersion\":1,\"guid\":\"guidFileless\",\"name\":\"dbonly\",\"type\":1,\"files\":[]}"));
        // and a real one for a live asset
        CHECK(db.initializeDatabase(dbPath), "database reopened to write a live sidecar");
        QString err;
        CHECK(AssetCas::writeSidecar(QSqlDatabase::database(), root, "guidShared", &err),
              "sidecar for the live shared asset");
        db.closeDatabase();

        const QString rebuiltDb = cwd + "/gc_rebuilt.db";
        QFile::remove(rebuiltDb);
        const auto rebuild = AssetMigration::rebuildCatalog(rebuiltDb, root);
        CHECK(rebuild.ok, "rebuildCatalog succeeded");
        CHECK(rebuild.skipped == 1, "exactly one sidecar was skipped as a tombstone");

        QSqlDatabase check = QSqlDatabase::addDatabase("QSQLITE", "GcRebuildCheck");
        check.setDatabaseName(rebuiltDb);
        check.open();
        CHECK(scalar(check, "SELECT COUNT(*) FROM assets WHERE guid = 'guidTombstone'") == 0,
              "THE DEFECT: the deleted asset was NOT resurrected");
        CHECK(scalar(check, "SELECT COUNT(*) FROM assets WHERE guid = 'guidFileless'") == 1,
              "a file-less (DB-only) sidecar IS still recovered");
        CHECK(scalar(check, "SELECT COUNT(*) FROM assets WHERE guid = 'guidShared'") == 1,
              "the live asset is recovered");
        check.close();
        check = QSqlDatabase();
        QSqlDatabase::removeDatabase("GcRebuildCheck");
        QFile::remove(rebuiltDb);
    }

    // ================= the offline root =================
    {
        CHECK(db.initializeDatabase(dbPath), "database reopened for the offline check");
        const auto offline = AssetGc::sweep(QSqlDatabase::database(),
                                            cwd + "/gc_no_such_root", /*dryRun*/ true);
        CHECK(!offline.ok && offline.error.contains("offline"),
              "a sweep of an unreachable root reports offline and collects nothing");
        CHECK(offline.totalCount() == 0, "…with an empty report");
        db.closeDatabase();
    }

    // ---- cleanup ----
    AssetStorePaths::setRootOverride(QString());
    QFile::remove(dbPath);
    QFile::remove(dbPath + ".lock");
    QDir(root).removeRecursively();
    QDir(srcDir).removeRecursively();

    printf(failures ? "FAILED: %d check(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
