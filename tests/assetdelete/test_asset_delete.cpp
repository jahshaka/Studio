// Headless round-trip test for library asset DELETION (the 2026-09-03 defect:
// every asset delete in a live session silently no-opped, the log carrying
// nothing but the SQLite driver's misleading "Parameter count mismatch" at
// [info] level).
//
// Covers, against the REAL Database class on a throwaway SQLite file:
//   1. deleteAsset UNLISTS an asset a project pins and touches nothing else
//      (owner law 2026-09-09: a library delete never takes an asset out of a
//      project); with force — or with no pins — it removes the asset row, its
//      asset_files content mapping AND its project_assets pins, and the delete
//      trigger decrements the shared files.refcount, so content still
//      referenced by another asset survives.
//   2. deleteAssetAndDependencies takes the dependency edges with it and
//      reports success through its `ok` out-param — judging every member of
//      the closure by its OWN pins.
//   3. A delete attempted on a CLOSED connection returns FALSE (never a silent
//      "true" over surviving rows) and leaves the in-memory AssetManager cache
//      alone — the regression that made the whole class of failures invisible.
//   4. updateGlobalDependencyDependee/Depender actually update a row (they
//      bound named placeholders against positional SQL and had never run).
//
// Plus the 2026-09 deep-audit (area 6) DB quick fixes:
//   5. deleteAsset is ONE transaction — a failure part-way leaves the asset
//      row, its content mapping and its pins all intact, never half of them.
//   6. deleteProject drops the project's project_assets pins (32 of 129 pins
//      on the measured live store were already orphaned this way), and the
//      dependency-name filters no longer skip an element after a removeAt.
//   7. wipeDatabase drops the CAS + favourites tables too, so the rebuilt
//      library does not open onto a content catalog for assets that are gone.
//   8. addFavorite is idempotent (INSERT OR REPLACE over a PRIMARY KEY).
//   9. The .jaf version gate parses integer components instead of running the
//      first three characters through toFloat()*10.
//  10. The pin reads (countAssetPins/fetchAssetPins), the library-listing
//      split (an unlisted row resolves by guid but is not a tile) and the
//      reaping of an unlisted row whose last pinning project is deleted.
//
// Plus the library-delete CODE REVIEW follow-ups (MASTER_QUEUE §26, item 5b):
//  11. DEAD PINS: a project_assets row whose project no longer exists counts
//      for nothing (countAssetPins JOINs projects, so it cannot veto a plain
//      delete) but is still reported by fetchAssetPins, flagged dead and
//      named "(deleted project)" instead of showing a raw guid.
//  12. deleteFolderAndDependencies obeys the pin law like its asset twin: a
//      pinned member is unlisted, its files are NOT handed back for unlink
//      and its dependency edges stay.
//  13. deleteProject ROLLS BACK when an orphan reap fails (it used to drop
//      the result and commit, leaving the project gone and an invisible,
//      unpinned, undeletable row behind), and (13b) a rolled-back reap leaves
//      the sidecar and the session registration alone — a nested delete's
//      scrubs wait for the OUTER commit.
//  14. .jaf archives carry `listed`: an export of an UNLISTED asset imports
//      unlisted, and an archive written before the column existed imports
//      listed.
//
// Framework-free; non-zero exit on failure. Runs under QT_QPA_PLATFORM=offscreen.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QVariant>
#include <cstdio>

#include "data/database/database.h"
#include "data/constants.h"
#include "io/assetmanager.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static int scalar(const QString &sql)
{
    QSqlQuery q;
    if (!q.exec(sql)) { printf("info: query error: %s\n", qPrintable(q.lastError().text())); return -1; }
    return q.next() ? q.value(0).toInt() : -1;
}

static int countWhere(const QString &table, const QString &col, const QString &value)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2 = ?").arg(table, col));
    q.addBindValue(value);
    if (!q.exec()) { printf("info: query error: %s\n", qPrintable(q.lastError().text())); return -1; }
    return q.next() ? q.value(0).toInt() : -1;
}

static int refcountOf(const QString &oid)
{
    QSqlQuery q;
    q.prepare("SELECT refcount FROM files WHERE oid = ?");
    q.addBindValue(oid);
    if (!q.exec()) return -1;
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

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets

    QTemporaryDir scratch;
    if (!scratch.isValid()) { printf("FAIL: no scratch dir\n"); return 1; }
    const QDir scratchDir(scratch.path());
    const QString storeRoot = scratchDir.filePath("store");
    QDir().mkpath(storeRoot);

    const QString dbPath = scratchDir.filePath("assetdelete_test.db");
    QFile::remove(dbPath);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    const QString projectGuid = "proj-delete-test";
    // The fixture project has to be a REAL row: since the code review of
    // 2026-09-10 a pin whose project does not exist is a DEAD pin and counts
    // for nothing (case 11 asserts exactly that), so a fixture without it
    // would be testing the dead-pin path everywhere by accident.
    CHECK(db.createProject(projectGuid, "Delete Test"), "fixture project row created");

    // --- Two assets, one SHARED content object, plus a private one ----------
    const QString sharedSrc = writeTempFile(scratchDir, "shared.png", QByteArray("shared-bytes"));
    const QString ownSrc    = writeTempFile(scratchDir, "own.png",    QByteArray("private-bytes"));
    CHECK(!sharedSrc.isEmpty() && !ownSrc.isEmpty(), "source files written");

    const QString victimGuid = db.createAssetEntry(
        "guid-victim", "victim.png", static_cast<int>(ModelTypes::Texture),
        QString(), projectGuid, QString(), QString(), QByteArray(), QByteArray(),
        QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
    const QString keeperGuid = db.createAssetEntry(
        "guid-keeper", "keeper.png", static_cast<int>(ModelTypes::Texture),
        QString(), projectGuid, QString(), QString(), QByteArray(), QByteArray(),
        QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
    CHECK(!victimGuid.isEmpty() && !keeperGuid.isEmpty(), "two asset rows created");

    QSqlDatabase conn = QSqlDatabase::database();
    QString sharedOid, ownOid, err;
    CHECK(AssetCas::ingestFile(conn, storeRoot, sharedSrc, victimGuid, "source", "shared.png",
                               &sharedOid, &err), "victim ingests the shared object");
    CHECK(AssetCas::ingestFile(conn, storeRoot, ownSrc, victimGuid, "file", "own.png",
                               &ownOid, &err), "victim ingests its private object");
    QString keeperOid;
    CHECK(AssetCas::ingestFile(conn, storeRoot, sharedSrc, keeperGuid, "source", "shared.png",
                               &keeperOid, &err), "keeper ingests the SAME shared object");
    CHECK(keeperOid == sharedOid, "content-addressed: one oid for identical bytes");

    CHECK(AssetCas::writePin(conn, projectGuid, victimGuid, sharedOid), "victim pinned into the project");
    CHECK(AssetCas::writePin(conn, projectGuid, keeperGuid, sharedOid), "keeper pinned into the project");

    // A dependency edge victim -> keeper, plus the row assets.remove's
    // keepShared branch deletes.
    CHECK(db.createDependency(static_cast<int>(ModelTypes::Texture),
                              static_cast<int>(ModelTypes::Texture),
                              victimGuid, keeperGuid, projectGuid),
          "dependency row created");

    // --- Pre-conditions -----------------------------------------------------
    CHECK(countWhere("assets", "guid", victimGuid) == 1, "victim asset row present");
    CHECK(countWhere("asset_files", "asset_guid", victimGuid) == 2, "victim has 2 asset_files rows");
    CHECK(countWhere("project_assets", "asset_guid", victimGuid) == 1, "victim has 1 project pin");
    CHECK(refcountOf(sharedOid) == 2, "shared object refcount 2 (victim + keeper)");
    CHECK(refcountOf(ownOid) == 1, "private object refcount 1");

    // --- 1a. A PINNED asset is UNLISTED, not deleted ------------------------
    //
    // The owner's law (2026-09-09): deleting from the library never takes an
    // asset out of a project. The victim is pinned, so the delete must change
    // exactly ONE thing — library visibility.
    CHECK(db.countAssetPins(victimGuid) == 1, "the victim is pinned by one project");
    CHECK(db.deleteAsset(victimGuid), "deleteAsset on a PINNED asset reports success");
    CHECK(countWhere("assets", "guid", victimGuid) == 1, "... and the asset row SURVIVES");
    CHECK(!db.isAssetListed(victimGuid), "... unlisted");
    CHECK(!db.fetchAsset(victimGuid).listed, "... which fetchAsset reports");
    CHECK(!db.fetchAsset(victimGuid).guid.isEmpty(),
          "... while the row still resolves BY GUID (every pin keeps working)");
    CHECK(countWhere("asset_files", "asset_guid", victimGuid) == 2,
          "... its content mapping is untouched (assets.gc still sees the bytes as live)");
    CHECK(countWhere("project_assets", "asset_guid", victimGuid) == 1, "... its pin is untouched");
    CHECK(refcountOf(sharedOid) == 2, "... and no refcount moved");
    {
        bool listed = false;
        for (const auto &row : db.fetchAssetsForAssetView())
            if (row.guid == victimGuid) listed = true;
        CHECK(!listed, "... the library listing no longer shows it");
    }

    // --- 1b. force takes it everywhere -------------------------------------
    CHECK(db.deleteAsset(victimGuid, /*force*/ true), "deleteAsset(force) reports success");
    CHECK(countWhere("assets", "guid", victimGuid) == 0, "asset row deleted");
    CHECK(countWhere("asset_files", "asset_guid", victimGuid) == 0, "asset_files rows deleted");
    CHECK(countWhere("project_assets", "asset_guid", victimGuid) == 0, "project pin deleted");
    CHECK(refcountOf(sharedOid) == 1, "shared object refcount decremented to 1 (keeper holds it)");
    CHECK(refcountOf(ownOid) == 0, "private object refcount decremented to 0 (GC-reapable)");

    CHECK(countWhere("assets", "guid", keeperGuid) == 1, "keeper untouched");
    CHECK(countWhere("asset_files", "asset_guid", keeperGuid) == 1, "keeper's content mapping untouched");
    CHECK(countWhere("project_assets", "asset_guid", keeperGuid) == 1, "keeper's pin untouched");

    // --- 2. deleteAssetAndDependencies ---------------------------------------
    const QString parentGuid = db.createAssetEntry(
        "guid-parent", "parent.obj", static_cast<int>(ModelTypes::Object),
        QString(), projectGuid, QString(), QString(), QByteArray(), QByteArray(),
        QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
    const QString childGuid = db.createAssetEntry(
        "guid-child", "child.png", static_cast<int>(ModelTypes::Texture),
        QString(), projectGuid, QString(), QString(), QByteArray(), QByteArray(),
        QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
    QString childOid;
    CHECK(AssetCas::ingestFile(conn, storeRoot, ownSrc, childGuid, "source", "child.png",
                               &childOid, &err), "child ingests content");
    CHECK(AssetCas::writePin(conn, projectGuid, childGuid, childOid), "child pinned");
    CHECK(db.createDependency(static_cast<int>(ModelTypes::Object),
                              static_cast<int>(ModelTypes::Texture),
                              parentGuid, childGuid, projectGuid),
          "parent -> child dependency created");

    bool ok = false;
    db.deleteAssetAndDependencies(parentGuid, &ok);
    CHECK(ok, "deleteAssetAndDependencies reports success");
    CHECK(countWhere("assets", "guid", parentGuid) == 0, "parent asset row deleted");
    // The CLOSURE obeys the same law, member by member: the child is pinned,
    // so it is unlisted and everything it needs to keep serving that pin —
    // its content mapping and the pin itself — stays.
    CHECK(countWhere("assets", "guid", childGuid) == 1, "pinned child asset row SURVIVES");
    CHECK(!db.isAssetListed(childGuid), "... unlisted");
    CHECK(countWhere("asset_files", "asset_guid", childGuid) == 1, "child content mapping kept");
    CHECK(countWhere("project_assets", "asset_guid", childGuid) == 1, "child pin kept");
    CHECK(refcountOf(childOid) == 1, "child's object is still referenced");
    CHECK(countWhere("dependencies", "depender", parentGuid) == 0, "dependency edge deleted");

    bool forcedOk = false;
    db.deleteAssetAndDependencies(childGuid, &forcedOk, /*force*/ true);
    CHECK(forcedOk, "deleteAssetAndDependencies(force) reports success");
    CHECK(countWhere("assets", "guid", childGuid) == 0, "forced: child asset row deleted");
    CHECK(countWhere("asset_files", "asset_guid", childGuid) == 0, "forced: content mapping deleted");
    CHECK(countWhere("project_assets", "asset_guid", childGuid) == 0, "forced: child pin deleted");
    CHECK(refcountOf(childOid) == 0, "child's object refcount back to 0");

    // --- 4. the sibling drift: positional SQL bound by name ------------------
    // (checked before the connection is closed for case 3)
    // NOTE: the `type` argument of these three functions is the row's
    // DEPENDER_TYPE (see getDependencyByType's WHERE clause), so the test keeps
    // it consistent with the row it creates.
    {
        const int dependerType = static_cast<int>(ModelTypes::Shader);
        const QString depender = "dep-er", oldDependee = "dep-ee-old", newDependee = "dep-ee-new";
        CHECK(db.createDependency(dependerType, static_cast<int>(ModelTypes::File),
                                  depender, oldDependee, projectGuid),
              "shader dependency row created");
        CHECK(db.getDependencyByType(dependerType, depender) == oldDependee,
              "dependency row reads back before the update");

        CHECK(db.updateGlobalDependencyDependee(dependerType, depender, newDependee),
              "updateGlobalDependencyDependee executes");
        CHECK(db.getDependencyByType(dependerType, depender) == newDependee,
              "updateGlobalDependencyDependee actually moved the dependee");

        CHECK(db.updateGlobalDependencyDepender(dependerType, "dep-er-new", newDependee),
              "updateGlobalDependencyDepender executes");
        CHECK(db.getDependencyByType(dependerType, "dep-er-new") == newDependee,
              "updateGlobalDependencyDepender actually moved the depender");
        CHECK(db.getDependencyByType(dependerType, depender).isEmpty(),
              "the old depender no longer owns the row");
    }

    // --- 3. a delete on a CLOSED connection must FAIL LOUDLY ----------------
    // This is the shutdown path that silently discarded every pending delete:
    // the connection object still exists but is closed, so prepare() fails and
    // the driver reports the misleading "Parameter count mismatch".
    const QString survivorGuid = db.createAssetEntry(
        "guid-survivor", "survivor.png", static_cast<int>(ModelTypes::Texture),
        QString(), projectGuid, QString(), QString(), QByteArray(), QByteArray(),
        QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
    CHECK(!survivorGuid.isEmpty(), "survivor asset row created");

    // Mirror what the AssetManager cache holds, so we can prove the cache is
    // NOT scrubbed by a failed delete.
    {
        auto *asset = new AssetVariant;
        asset->assetGuid = survivorGuid;
        asset->type = ModelTypes::Texture;
        AssetManager::addAsset(asset);
    }
    CHECK(AssetManager::getAssets().count() == 1, "asset cached in AssetManager");

    QSqlDatabase::database().close();
    CHECK(!QSqlDatabase::database(QLatin1String(QSqlDatabase::defaultConnection), false).isOpen(),
          "connection closed (the shutdown situation)");

    CHECK(!db.deleteAsset(survivorGuid), "deleteAsset on a closed connection returns FALSE");
    CHECK(AssetManager::getAssets().count() == 1,
          "a refused delete does NOT scrub the in-memory cache");

    bool okClosed = true;
    db.deleteAssetAndDependencies(survivorGuid, &okClosed);
    CHECK(!okClosed, "deleteAssetAndDependencies on a closed connection reports failure");

    // Reopen and prove the row really did survive (i.e. the FALSE was honest).
    CHECK(QSqlDatabase::database().open(), "connection reopened");
    CHECK(countWhere("assets", "guid", survivorGuid) == 1,
          "the refused delete left the asset row in place");

    AssetManager::clearAssetList();

    // --- 5. deleteAsset is ATOMIC -------------------------------------------
    // Force the middle statement to fail (the pins table is gone) and prove
    // nothing moved: before the transaction landed, the asset_files DELETE had
    // already run and the assets DELETE ran after it, so a "failed" delete
    // still destroyed the row and its content mapping — an asset that vanished
    // from the library while the function reported failure.
    {
        conn = QSqlDatabase::database();
        const QString atomicGuid = db.createAssetEntry(
            "guid-atomic", "atomic.png", static_cast<int>(ModelTypes::Texture),
            QString(), projectGuid, QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        QString atomicOid;
        CHECK(AssetCas::ingestFile(conn, storeRoot, ownSrc, atomicGuid, "source", "own.png",
                                   &atomicOid, &err), "atomicity fixture ingests content");
        CHECK(AssetCas::writePin(conn, projectGuid, atomicGuid, atomicOid),
              "atomicity fixture pinned");
        CHECK(refcountOf(atomicOid) == 1, "atomicity fixture refcount 1");

        QSqlQuery drop;
        CHECK(drop.exec("DROP TABLE project_assets"),
              "pins table dropped to force a mid-delete failure");

        CHECK(!db.deleteAsset(atomicGuid), "deleteAsset reports FALSE when a statement fails");
        CHECK(countWhere("assets", "guid", atomicGuid) == 1,
              "atomic: the asset row survived the failed delete");
        CHECK(countWhere("asset_files", "asset_guid", atomicGuid) == 1,
              "atomic: the content mapping survived the failed delete");
        CHECK(refcountOf(atomicOid) == 1,
              "atomic: the refcount the delete trigger touched was rolled back too");

        db.createCasTables();   // put the pins table back for the sections below
        CHECK(db.checkIfTableExists("project_assets"), "pins table restored");
        CHECK(db.deleteAsset(atomicGuid), "the same delete succeeds once the table is back");
        CHECK(countWhere("assets", "guid", atomicGuid) == 0, "atomic: asset finally deleted");
    }

    // --- 6. deleteProject takes its pins with it ----------------------------
    {
        conn = QSqlDatabase::database();
        const QString doomedProject = "proj-doomed";
        CHECK(db.createProject(doomedProject, "Doomed"), "doomed project created");

        const QString pinnedGuid = db.createAssetEntry(
            "guid-pinned", "pinned.png", static_cast<int>(ModelTypes::Texture),
            QString(), doomedProject, QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        QString pinnedOid;
        CHECK(AssetCas::ingestFile(conn, storeRoot, sharedSrc, pinnedGuid, "source", "shared.png",
                                   &pinnedOid, &err), "project asset ingested");
        CHECK(AssetCas::writePin(conn, doomedProject, pinnedGuid, pinnedOid),
              "asset pinned into the doomed project");
        CHECK(db.createDependency(static_cast<int>(ModelTypes::Texture),
                                  static_cast<int>(ModelTypes::Texture),
                                  pinnedGuid, keeperGuid, doomedProject),
              "project-scoped dependency created");
        CHECK(countWhere("project_assets", "project_guid", doomedProject) == 1,
              "the pin exists before the project is deleted");

        CHECK(db.deleteProject(doomedProject), "deleteProject reports success");
        CHECK(countWhere("projects", "guid", doomedProject) == 0, "project row deleted");
        CHECK(countWhere("dependencies", "project_guid", doomedProject) == 0,
              "project dependencies deleted");
        CHECK(countWhere("project_assets", "project_guid", doomedProject) == 0,
              "project PINS deleted with the project (the orphaned-pin leak)");
    }

    // --- 6b. the dependency name filter does not skip after a removeAt ------
    // fetchAssetAndDependencies drops dependency NAMES that carry no file
    // extension (they are not files to unlink). The forward loop removed the
    // first bare name, the tail shifted down, and ++i then stepped over the
    // name that had moved into the vacated index — so with two adjacent bare
    // names the SECOND one survived into the caller's delete list. Asserted on
    // the fetcher directly: the delete paths run the same filter a second time,
    // which masks the defect for non-adjacent survivors.
    {
        const QString ownerGuid = db.createAssetEntry(
            "guid-filter-owner", "owner.obj", static_cast<int>(ModelTypes::Object),
            QString(), projectGuid, QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        // Dependees whose NAMES have no suffix — exactly what the filter drops.
        const QString bare1 = db.createAssetEntry(
            "guid-bare-one", "bareone", static_cast<int>(ModelTypes::Texture),
            QString(), projectGuid, QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        const QString bare2 = db.createAssetEntry(
            "guid-bare-two", "baretwo", static_cast<int>(ModelTypes::Texture),
            QString(), projectGuid, QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        CHECK(db.createDependency(static_cast<int>(ModelTypes::Object),
                                  static_cast<int>(ModelTypes::Texture),
                                  ownerGuid, bare1, projectGuid), "bare dependency 1");
        CHECK(db.createDependency(static_cast<int>(ModelTypes::Object),
                                  static_cast<int>(ModelTypes::Texture),
                                  ownerGuid, bare2, projectGuid), "bare dependency 2");

        const QStringList names = db.fetchAssetAndDependencies(ownerGuid);
        CHECK(names.contains("owner.obj"), "the owner's own file name is kept");
        CHECK(!names.contains("bareone") && !names.contains("baretwo"),
              "BOTH extension-less names filtered out (no index-skipping survivor)");

        bool filterOk = false;
        const QStringList files = db.deleteAssetAndDependencies(ownerGuid, &filterOk);
        CHECK(filterOk, "deleteAssetAndDependencies over the bare-named set succeeded");
        CHECK(!files.contains("bareone") && !files.contains("baretwo"),
              "and the delete path's list is clean too");
    }

    // --- 8. addFavorite is idempotent ---------------------------------------
    {
        CHECK(db.addFavorite(keeperGuid), "asset favourited");
        CHECK(db.addFavorite(keeperGuid),
              "favouriting an already-favourite asset succeeds (INSERT OR REPLACE)");
        CHECK(countWhere("favorites", "asset_guid", keeperGuid) == 1,
              "exactly one favourites row for the asset");
        CHECK(db.removeFavorite(keeperGuid), "favourite removed");
        CHECK(countWhere("favorites", "asset_guid", keeperGuid) == 0, "favourites row gone");
    }

    // --- 9. the .jaf version gate (MIN_JAF_VERSION = 9, i.e. "0.9") ---------
    // The old gate took version.mid(0,3).toFloat() * 10 and truncated to int:
    // correct for "0.9" only because 0.9f*10 rounds up to exactly 9.0f, and
    // flatly wrong for any 0.10.x archive ("0.1" -> 1 -> rejected).
    {
        CHECK(Constants::MIN_JAF_VERSION == 9, "the gate under test is still 0.9");
        CHECK(Database::jafVersionAccepted("0.9.1b"),  "0.9.1b accepted (the shipped version)");
        CHECK(!Database::jafVersionAccepted("0.8.0"),  "0.8.0 rejected");
        CHECK(Database::jafVersionAccepted("1.0.0"),   "1.0.0 accepted");
        CHECK(Database::jafVersionAccepted("1.10"),    "1.10 accepted");
        CHECK(Database::jafVersionAccepted("0.9"),     "0.9 accepted (exactly the minimum)");
        CHECK(Database::jafVersionAccepted("0.10.0"),  "0.10.0 accepted (0.10 > 0.9, not 0.1)");
        CHECK(!Database::jafVersionAccepted("0.0.1"),  "0.0.1 rejected");
        CHECK(!Database::jafVersionAccepted(""),       "an empty version is rejected");
        CHECK(!Database::jafVersionAccepted("beta"),   "a non-numeric version is rejected");
    }

    // --- 10. LIBRARY DELETE KEEPS PROJECT PINS: the reads and the reaping ---
    //
    // The pin read the Assets page and `assets.pins` share, the listing split
    // (unlisted rows resolve by guid but are not tiles), and the one thing an
    // unlist must not leak: a row nobody can see and nobody pins.
    {
        const QString projA = "proj-pins-a", projB = "proj-pins-b";
        CHECK(db.createProject(projA, "Kitchen"), "project A created");
        CHECK(db.createProject(projB, "Showroom"), "project B created");

        const QString shared = db.createAssetEntry(
            "guid-two-pins", "two-pins.png", static_cast<int>(ModelTypes::Texture),
            QString(), QString(), QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        CHECK(!shared.isEmpty(), "the two-pin asset row created");
        QString oid, e;
        CHECK(AssetCas::ingestFile(conn, storeRoot, ownSrc, shared, "source", "two-pins.png", &oid, &e),
              "the pinned asset has content");
        CHECK(AssetCas::writePin(conn, projA, shared, oid), "project A pins it");
        CHECK(AssetCas::writePin(conn, projB, shared, oid), "project B pins it");

        CHECK(db.countAssetPins(shared) == 2, "countAssetPins sees both projects");
        const auto pins = db.fetchAssetPins(shared);
        CHECK(pins.size() == 2, "fetchAssetPins returns both");
        QStringList names;
        for (const auto &pin : pins) names << pin.projectName;
        CHECK(names.contains("Kitchen") && names.contains("Showroom"),
              "... named by their PROJECT names, not their guids");

        bool tile = false;
        for (const auto &row : db.fetchAssetsForAssetView()) if (row.guid == shared) tile = true;
        CHECK(tile, "the library listing shows it while it is listed");

        CHECK(db.deleteAsset(shared), "the library delete succeeds");
        tile = false;
        for (const auto &row : db.fetchAssetsForAssetView()) if (row.guid == shared) tile = true;
        CHECK(!tile, "... and it stops being a library tile");
        CHECK(db.countAssetPins(shared) == 2, "... with both pins intact");
        CHECK(!db.fetchLibraryAssetGuids().contains(shared),
              "... and out of the library guid scan too");

        // Re-listing, the other direction: an import of the same content
        // brings the row back (services/import — asserted here at the write
        // it performs, which is all this layer owns).
        CHECK(db.setAssetListed(shared, true), "setAssetListed(true) re-lists it");
        tile = false;
        for (const auto &row : db.fetchAssetsForAssetView()) if (row.guid == shared) tile = true;
        CHECK(tile, "... and the tile is back");
        CHECK(db.deleteAsset(shared), "unlisted again for the reaping check");

        // The one leak an unlist could cause: deleting the LAST project that
        // pinned an unlisted row must finish the delete the user asked for.
        CHECK(db.deleteProject(projA), "project A deleted");
        CHECK(countWhere("assets", "guid", shared) == 1,
              "the unlisted row survives while project B still pins it");
        CHECK(db.deleteProject(projB), "project B deleted");
        CHECK(countWhere("assets", "guid", shared) == 0,
              "the last pin gone: the unlisted row is finally deleted");
        CHECK(countWhere("asset_files", "asset_guid", shared) == 0,
              "... with its content mapping, so assets.gc can reclaim the bytes");
        CHECK(!db.setAssetListed(shared, true), "setAssetListed on an unknown guid is FALSE");
    }

    // --- 11. DEAD PINS: a pin whose project is gone counts for nothing ------
    //
    // 32 of the 129 pins on the owner's measured store named projects that no
    // longer existed. Counted as pins they made an asset NO LIVING PROJECT
    // used undeletable through the normal path (the delete unlisted it and the
    // grid lost it), and the Assets page showed a raw guid as the "project"
    // using it. So: countAssetPins weighs LIVE pins only; fetchAssetPins still
    // reports the dead row (it is a real reference — assets.gc reaps it) and
    // names it for a human.
    {
        conn = QSqlDatabase::database();
        const QString ghost = "proj-that-never-existed";
        const QString hauntedGuid = db.createAssetEntry(
            "guid-haunted", "haunted.png", static_cast<int>(ModelTypes::Texture),
            QString(), QString(), QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        QString hauntedOid, he;
        CHECK(AssetCas::ingestFile(conn, storeRoot, ownSrc, hauntedGuid, "source", "haunted.png",
                                   &hauntedOid, &he), "the haunted asset has content");
        CHECK(AssetCas::writePin(conn, ghost, hauntedGuid, hauntedOid),
              "a pin written for a project that does not exist");
        CHECK(countWhere("project_assets", "asset_guid", hauntedGuid) == 1,
              "the dead pin row is really there");

        CHECK(db.countAssetPins(hauntedGuid) == 0,
              "countAssetPins ignores a pin whose project is gone");
        const auto ghostPins = db.fetchAssetPins(hauntedGuid);
        CHECK(ghostPins.size() == 1, "fetchAssetPins still REPORTS the dead pin");
        CHECK(!ghostPins.first().live, "... flagged dead");
        CHECK(ghostPins.first().projectName != ghost && !ghostPins.first().projectName.isEmpty(),
              qPrintable(QStringLiteral("... and named for a human, not by its guid: %1")
                             .arg(ghostPins.first().projectName)));

        // The point of the fix: a plain library delete really deletes now.
        CHECK(db.deleteAsset(hauntedGuid), "the plain library delete succeeds");
        CHECK(countWhere("assets", "guid", hauntedGuid) == 0,
              "... and the asset row is GONE (a dead pin cannot veto a delete)");
        CHECK(countWhere("project_assets", "asset_guid", hauntedGuid) == 0,
              "... the dead pin went with it");

        // A LIVE pin still vetoes, in the same session, on the same shape.
        const QString livingGuid = db.createAssetEntry(
            "guid-living", "living.png", static_cast<int>(ModelTypes::Texture),
            QString(), QString(), QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        QString livingOid;
        CHECK(AssetCas::ingestFile(conn, storeRoot, ownSrc, livingGuid, "source", "living.png",
                                   &livingOid, &he), "the living asset has content");
        CHECK(AssetCas::writePin(conn, projectGuid, livingGuid, livingOid),
              "pinned by the (existing) fixture project");
        CHECK(db.countAssetPins(livingGuid) == 1, "a LIVE pin counts");
        CHECK(db.fetchAssetPins(livingGuid).first().live, "... and reads back live");
        CHECK(db.deleteAsset(livingGuid) && !db.isAssetListed(livingGuid),
              "... so the library delete is still an UNLIST");
        CHECK(db.deleteAsset(livingGuid, /*force*/ true), "cleaned up with force");
    }

    // --- 12. deleteFolderAndDependencies obeys the pin law ------------------
    //
    // The folder delete never learned it (code review 2026-09-10): it deleted
    // a pinned member's dependency edges and handed its file names back to the
    // caller to UNLINK, so removing a library folder took the bytes out from
    // under every project that pinned what was in it.
    {
        conn = QSqlDatabase::database();
        const QString folderGuid = "folder-pin-law";
        CHECK(db.createFolder("Pinned Folder", QString(), folderGuid, QString()),
              "library folder created");

        const QString inFolder = db.createAssetEntry(
            "guid-in-folder", "infolder.png", static_cast<int>(ModelTypes::Texture),
            folderGuid, QString(), QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        const QString folderDep = db.createAssetEntry(
            "guid-folder-dep", "folderdep.png", static_cast<int>(ModelTypes::Texture),
            folderGuid, QString(), QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        QString inFolderOid, fe;
        CHECK(AssetCas::ingestFile(conn, storeRoot, ownSrc, inFolder, "source", "infolder.png",
                                   &inFolderOid, &fe), "the folder member has content");
        CHECK(db.createDependency(static_cast<int>(ModelTypes::Texture),
                                  static_cast<int>(ModelTypes::Texture),
                                  inFolder, folderDep, QString()),
              "the folder member has a dependency edge");
        CHECK(AssetCas::writePin(conn, projectGuid, inFolder, inFolderOid),
              "a project pins the folder member");

        bool folderOk = false;
        const QStringList unlinkList = db.deleteFolderAndDependencies(folderGuid, &folderOk);
        CHECK(folderOk, "deleteFolderAndDependencies reports success");
        CHECK(countWhere("assets", "guid", inFolder) == 1,
              "the PINNED folder member survives as a row");
        CHECK(!db.isAssetListed(inFolder), "... unlisted");
        CHECK(countWhere("asset_files", "asset_guid", inFolder) == 1,
              "... its content mapping is intact");
        CHECK(countWhere("project_assets", "asset_guid", inFolder) == 1, "... so is its pin");
        CHECK(countWhere("dependencies", "depender", inFolder) == 1,
              "... and its dependency EDGE stays (the project resolves through it)");
        CHECK(!unlinkList.contains("infolder.png"),
              "... and its file is NOT handed back for unlinking");
        CHECK(refcountOf(inFolderOid) == 1, "... the object is still referenced");
        // The UNPINNED sibling went, folder and all — the law is per member.
        CHECK(countWhere("assets", "guid", folderDep) == 0, "the unpinned member was deleted");
        CHECK(countWhere("folders", "guid", folderGuid) == 0, "the folder row went");
    }

    // --- 13. deleteProject rolls back over a FAILED orphan reap -------------
    //
    // The reap's result was dropped and the transaction committed anyway: the
    // project went, the orphan stayed — invisible (unlisted), unpinned and,
    // because nothing lists it, undeletable forever. Forced here by removing
    // the table the reap's own delete needs.
    {
        conn = QSqlDatabase::database();
        const QString fragile = "proj-fragile";
        CHECK(db.createProject(fragile, "Fragile"), "fragile project created");
        const QString orphanGuid = db.createAssetEntry(
            "guid-orphan-reap", "orphan.png", static_cast<int>(ModelTypes::Texture),
            QString(), QString(), QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        QString orphanOid, oe;
        CHECK(AssetCas::ingestFile(conn, storeRoot, ownSrc, orphanGuid, "source", "orphan.png",
                                   &orphanOid, &oe), "the future orphan has content");
        CHECK(AssetCas::writePin(conn, fragile, orphanGuid, orphanOid), "pinned by the project");
        CHECK(db.deleteAsset(orphanGuid) && !db.isAssetListed(orphanGuid),
              "removed from the library while pinned (so it is unlisted)");

        QSqlQuery dropFiles;
        CHECK(dropFiles.exec("DROP TABLE asset_files"),
              "asset_files dropped to make the reap's own delete fail");
        CHECK(!db.deleteProject(fragile), "deleteProject reports FAILURE");
        CHECK(countWhere("projects", "guid", fragile) == 1,
              "... and the PROJECT row survived (the whole delete rolled back)");
        CHECK(countWhere("project_assets", "asset_guid", orphanGuid) == 1,
              "... the pin survived too");

        db.createCasTables();
        CHECK(db.checkIfTableExists("asset_files"), "asset_files restored");
        CHECK(db.deleteProject(fragile), "the same delete succeeds once the table is back");
        CHECK(countWhere("projects", "guid", fragile) == 0, "project deleted");
        CHECK(countWhere("assets", "guid", orphanGuid) == 0,
              "and the orphaned unlisted row was reaped this time");
    }

    // --- 13b. a ROLLED-BACK reap leaves the sidecar and the registry alone --
    //
    // The other half of the same defect (code review 2026-09-10). deleteAsset
    // scrubs the asset's sidecar and its session registration as soon as its
    // own statements succeed — but nested inside deleteProject's transaction
    // "succeeded" is not "durable". With two orphans, the first reaped and the
    // second failing, the rollback brought the first one's ROWS back while its
    // sidecar was already deleted: rebuildCatalog would then have lost that
    // asset for good, and the session had dropped it too. The scrubs are now
    // deferred to the outer commit.
    {
        conn = QSqlDatabase::database();
        // THE STORE ROOT HAS TO BE THIS ONE for the sidecar half to mean
        // anything: sidecarBelongsHere/dropSidecar read the process-global
        // AssetStorePaths::root(), so without the override they answer about
        // the developer's real library and every sidecar assertion below would
        // pass vacuously. Scoped to this case — the rest of the suite is
        // deliberately root-agnostic.
        AssetStorePaths::setRootOverride(storeRoot);
        const QString shaky = "proj-shaky";
        CHECK(db.createProject(shaky, "Shaky"), "shaky project created");

        const QString firstGuid = db.createAssetEntry(
            "guid-reap-first", "first.png", static_cast<int>(ModelTypes::Texture),
            QString(), QString(), QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        const QString secondGuid = db.createAssetEntry(
            "guid-reap-second", "second.png", static_cast<int>(ModelTypes::Texture),
            QString(), QString(), QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        QString firstOid, secondOid, re;
        CHECK(AssetCas::ingestFile(conn, storeRoot, sharedSrc, firstGuid, "source", "first.png",
                                   &firstOid, &re), "the first orphan has content");
        CHECK(AssetCas::ingestFile(conn, storeRoot, ownSrc, secondGuid, "source", "second.png",
                                   &secondOid, &re), "the second orphan has content");
        CHECK(AssetCas::writeSidecar(conn, storeRoot, firstGuid, &re),
              "the first orphan has a sidecar (the rebuild record)");
        CHECK(AssetCas::writePin(conn, shaky, firstGuid, firstOid), "first pinned by the project");
        CHECK(AssetCas::writePin(conn, shaky, secondGuid, secondOid), "second pinned too");
        CHECK(db.deleteAsset(firstGuid) && !db.isAssetListed(firstGuid), "first unlisted");
        CHECK(db.deleteAsset(secondGuid) && !db.isAssetListed(secondGuid), "second unlisted");

        const QString sidecarPath = AssetStorePaths::sidecarPathIn(storeRoot, firstGuid);
        CHECK(QFileInfo::exists(sidecarPath), "the sidecar file is on disk before the delete");

        // The session registration of the first orphan — the other thing the
        // scrub used to take before the rows were durable.
        {
            auto *asset = new AssetVariant;
            asset->assetGuid = firstGuid;
            asset->type = ModelTypes::Texture;
            AssetManager::addAsset(asset);
        }

        // The SECOND reap fails, the first has already succeeded: a trigger
        // that aborts exactly one row's delete, which is the shape a locked
        // table or a constraint would take in the wild.
        QSqlQuery trigger;
        CHECK(trigger.exec("CREATE TRIGGER block_second BEFORE DELETE ON assets "
                           "WHEN OLD.guid = 'guid-reap-second' "
                           "BEGIN SELECT RAISE(ABORT, 'refused'); END"),
              "a trigger that refuses the second reap");

        CHECK(!db.deleteProject(shaky), "deleteProject reports FAILURE");
        CHECK(countWhere("projects", "guid", shaky) == 1, "... the project row came back");
        CHECK(countWhere("assets", "guid", firstGuid) == 1,
              "... AND the first orphan's row came back");
        CHECK(QFileInfo::exists(sidecarPath),
              "THE FIX: its sidecar came back with it — the rebuild record still names it");
        {
            bool registered = false;
            for (auto *asset : AssetManager::getAssets())
                if (asset && asset->assetGuid == firstGuid) registered = true;
            CHECK(registered, "... and the session registration was never dropped");
        }

        QSqlQuery dropTrigger;
        CHECK(dropTrigger.exec("DROP TRIGGER block_second"), "the trigger is removed");
        CHECK(db.deleteProject(shaky), "the same delete succeeds once it can run");
        CHECK(countWhere("assets", "guid", firstGuid) == 0, "both orphans reaped this time");
        CHECK(countWhere("assets", "guid", secondGuid) == 0, "... the second too");
        CHECK(!QFileInfo::exists(sidecarPath),
              "... and NOW the sidecar goes, because the delete is real");
        {
            bool registered = false;
            for (auto *asset : AssetManager::getAssets())
                if (asset && asset->assetGuid == firstGuid) registered = true;
            CHECK(!registered, "... and the session registration goes with it");
        }
        AssetManager::clearAssetList();
        AssetStorePaths::setRootOverride(QString());
    }

    // --- 14. .jaf archives carry library visibility -------------------------
    //
    // An archive of an UNLISTED asset must land unlisted where it arrives: the
    // row the exporter had deleted from their library does not come back as a
    // library tile on someone else's box. Written by the ASSET export writers
    // (assetsTableSchema), read tolerantly — an archive from before the column
    // existed has no opinion and imports listed.
    {
        conn = QSqlDatabase::database();
        const QString travellerGuid = db.createAssetEntry(
            "guid-traveller", "traveller.png", static_cast<int>(ModelTypes::Texture),
            QString(), QString(), QString(), QString(), QByteArray(), QByteArray(),
            QByteArray(), QByteArray(), AssetViewFilter::AssetsView);
        QString travellerOid, te;
        CHECK(AssetCas::ingestFile(conn, storeRoot, ownSrc, travellerGuid, "source",
                                   "traveller.png", &travellerOid, &te),
              "the travelling asset has content");
        CHECK(AssetCas::writePin(conn, projectGuid, travellerGuid, travellerOid),
              "pinned, so the library delete unlists it");
        CHECK(db.deleteAsset(travellerGuid) && !db.isAssetListed(travellerGuid),
              "the exported asset is UNLISTED");

        const QString archive = scratchDir.filePath("traveller-asset.db");
        CHECK(db.createBlobFromAsset(travellerGuid, archive), "asset archive written");

        QMap<QString, QString> guidMap;
        QVector<AssetRecord> imported;
        const QString landedGuid = db.importAsset(ModelTypes::Texture, archive,
                                                  QMap<QString, QString>(), guidMap, imported,
                                                  AssetViewFilter::AssetsView, QString());
        CHECK(!landedGuid.isEmpty(),
              qPrintable(QStringLiteral("the archive imported -> %1").arg(landedGuid)));
        CHECK(countWhere("assets", "guid", landedGuid) == 1, "... as a new row");
        CHECK(!db.isAssetListed(landedGuid),
              "... UNLISTED, exactly as it was when it was exported");

        // And an archive from before the column: same file, column removed.
        const QString oldArchive = scratchDir.filePath("traveller-old.db");
        CHECK(QFile::copy(archive, oldArchive), "a second copy of the archive");
        {
            QSqlDatabase legacy = QSqlDatabase::addDatabase(Constants::DB_DRIVER, "LegacyJaf");
            legacy.setDatabaseName(oldArchive);
            CHECK(legacy.open(), "the copy opened for surgery");
            QSqlQuery drop(legacy);
            CHECK(drop.exec("ALTER TABLE assets DROP COLUMN listed"),
                  "the `listed` column removed (a pre-2026-09-10 archive)");
            legacy.close();
        }
        QSqlDatabase::removeDatabase("LegacyJaf");

        QMap<QString, QString> oldMap;
        QVector<AssetRecord> oldImported;
        const QString oldLanded = db.importAsset(ModelTypes::Texture, oldArchive,
                                                 QMap<QString, QString>(), oldMap, oldImported,
                                                 AssetViewFilter::AssetsView, QString());
        CHECK(!oldLanded.isEmpty(),
              qPrintable(QStringLiteral("the legacy archive imported -> %1").arg(oldLanded)));
        CHECK(db.isAssetListed(oldLanded),
              "... LISTED: an archive with no opinion about visibility has one made for it");
    }

    // --- 7. wipeDatabase clears the CAS catalog too (DESTRUCTIVE — last) ----
    {
        CHECK(db.checkIfTableExists("files") && db.checkIfTableExists("asset_files")
                  && db.checkIfTableExists("project_assets") && db.checkIfTableExists("favorites"),
              "CAS + favourites tables present before the wipe");
        db.wipeDatabase();
        CHECK(!db.checkIfTableExists("assets"), "wipe: assets dropped");
        CHECK(!db.checkIfTableExists("files"), "wipe: files dropped");
        CHECK(!db.checkIfTableExists("asset_files"), "wipe: asset_files dropped");
        CHECK(!db.checkIfTableExists("project_assets"), "wipe: project_assets dropped");
        CHECK(!db.checkIfTableExists("favorites"), "wipe: favorites dropped");
    }

    db.closeDatabase();
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("all asset-delete checks passed\n");
    return 0;
}
