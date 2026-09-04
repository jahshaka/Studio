// Headless round-trip test for library asset DELETION (the 2026-09-03 defect:
// every asset delete in a live session silently no-opped, the log carrying
// nothing but the SQLite driver's misleading "Parameter count mismatch" at
// [info] level).
//
// Covers, against the REAL Database class on a throwaway SQLite file:
//   1. deleteAsset removes the asset row, its asset_files content mapping AND
//      its project_assets pins, and the delete trigger decrements the shared
//      files.refcount — content still referenced by another asset survives.
//   2. deleteAssetAndDependencies takes the dependency edges with it and
//      reports success through its `ok` out-param.
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

    // --- 1. deleteAsset cleans all three tables and the refcounts -----------
    CHECK(db.deleteAsset(victimGuid), "deleteAsset reports success");
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
    CHECK(countWhere("assets", "guid", childGuid) == 0, "dependent child asset row deleted");
    CHECK(countWhere("asset_files", "asset_guid", childGuid) == 0, "child content mapping deleted");
    CHECK(countWhere("project_assets", "asset_guid", childGuid) == 0, "child pin deleted");
    CHECK(countWhere("dependencies", "depender", parentGuid) == 0, "dependency edge deleted");
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
