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
    db.closeDatabase();
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("all asset-delete checks passed\n");
    return 0;
}
