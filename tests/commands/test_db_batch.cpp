// commands.db_batch — CLOSE-2: ONE GESTURE, ONE COMMIT.
//
// CLOSE-1 made a dying undo command stop writing to disk. This is the other
// half of the same shape, on the CREATE side and on the command paths that
// write several rows for one user action:
//
//   * SceneEditService::addBuiltinPrimitive writes an asset row per primitive
//     (Database::createAssetEntry) with no transaction — SQLite autocommits it,
//     and an autocommit is a real transaction: journal, write, fdatasync,
//     unlink. A script that adds 300 primitives paid that 300+ times, one at a
//     time, on the UI thread.
//   * ResetMaterialCommand::redo/undo write a pin per default texture, an edge
//     deleted per dropped map and an edge created per default map — a dozen
//     autocommits for one click of Reset on a six-texture material.
//
// The fix is a COUNTED transaction scope on Database (beginBatch/endBatch, and
// the DbBatch guard): the editor opens one for the lifetime of a SCRIPT RUN —
// a run is already one undo macro, which is the gesture boundary — and the
// multi-write commands open one of their own.
//
// WHAT THIS SUITE PINS
//   1. N asset rows written inside a batch cost ONE commit; the same N outside
//      one cost N.
//   2. The rows are really there after the batch closes, and nothing is
//      visible to a second connection before it (the scope is a real
//      transaction, not bookkeeping).
//   3. Batches NEST by count: only the outermost commits.
//   4. A batch STANDS DOWN for a real transaction owner. This is the property
//      that keeps the import rollback honest: a DbTransaction opened inside a
//      batch must be its OWN transaction, or services/import/
//      assetimportservice.cpp's rollback silently becomes a no-op and a failed
//      import's rows ride the batch's commit.
//   5. closeDatabase() never leaves a batch open (the rows would be rolled
//      back inside the driver's teardown).
//   6. ResetMaterialCommand's redo and undo each cost ONE commit.
//   7. Database::durableCommits() — the counter behind
//      editor.undoState().dbCommits — agrees with SQLite's own commit hook.
//   8. The guard count is PER CONNECTION: a transaction on a side connection
//      wrapped around a main-connection guard does not make that guard look
//      nested (it would then degrade, which is the failure in 4).
//
// The COMMIT COUNT comes from sqlite3_commit_hook on the handle Qt's QSQLITE
// driver opened (the CLOSE-1 / tests/assettray idiom): ground truth, with no
// instrumentation in the production path. Framework-free, offscreen,
// DISPLAY-FREE.
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlQuery>
#include <QVariant>
#include <cstdio>

#include "commands/resetmaterialcommand.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"

#include "../support/documentgraph.h"
#include "services/projectassets.h"

#ifdef JAH_HAVE_SQLITE3
#include <sqlite3.h>
#endif

// LINK STUB (the tests/commands idiom). ResetMaterialCommand re-pins the
// default's own textures through ProjectAssets, whose real implementation
// drags the asset store, the material reader and the mesh-bake cache into a
// suite whose subject is transaction shape. The suite passes an EMPTY
// `newlyPinned` list, so the real body is never reached — only the symbol is.
ProjectAssets::Result ProjectAssets::addToProject(const QString &guid, Database *, Project *,
                                                  ProjectAssets::AddKind)
{
    ProjectAssets::Result result;
    result.guid = guid;
    return result;
}

static int failures = 0;
static int checks = 0;
#define CHECK(cond, msg) do { ++checks; if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static int gCommits = 0;

#ifdef JAH_HAVE_SQLITE3
static int commitCb(void *) { ++gCommits; return 0; }
static bool installCommitHook()
{
    QVariant handle = QSqlDatabase::database().driver()->handle();
    if (!handle.isValid() || qstrcmp(handle.typeName(), "sqlite3*") != 0) return false;
    sqlite3 *h = *static_cast<sqlite3 **>(handle.data());
    if (!h) return false;
    sqlite3_commit_hook(h, commitCb, nullptr);
    return true;
}
#else
static bool installCommitHook() { return false; }
#endif

namespace {

struct Measured { int commits = 0; quint64 counted = 0; qint64 micros = 0; };

template <typename Fn>
Measured measure(Fn fn)
{
    Measured m;
    QElapsedTimer timer;
    gCommits = 0;
    const quint64 before = Database::durableCommits();
    timer.start();
    fn();
    m.micros = timer.nsecsElapsed() / 1000;
    m.commits = gCommits;
    m.counted = Database::durableCommits() - before;
    return m;
}

/// An asset row exactly as SceneEditService::addBuiltinPrimitive writes one.
QString makePrimitiveRow(Database &db, const QString &projectGuid, int n)
{
    const QString guid = GUIDManager::generateGUID();
    QJsonObject props;
    props["type"] = "builtin";
    db.createAssetEntry(guid, QStringLiteral("Cube %1").arg(n),
                        static_cast<int>(ModelTypes::Object),
                        projectGuid, projectGuid, QString(), QString(), QByteArray(),
                        QJsonDocument(props).toJson(), QByteArray(), QByteArray());
    return guid;
}

int assetsNamed(const QString &prefix)
{
    QSqlQuery q;
    q.prepare("SELECT COUNT(*) FROM assets WHERE name LIKE ?");
    q.addBindValue(prefix + QStringLiteral("%"));
    return (q.exec() && q.next()) ? q.value(0).toInt() : -1;
}

int edgeCount(const QString &depender)
{
    QSqlQuery q;
    q.prepare("SELECT COUNT(*) FROM dependencies WHERE depender = ?");
    q.addBindValue(depender);
    return (q.exec() && q.next()) ? q.value(0).toInt() : -1;
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets (QMessageBox)
    enginetest::DocumentGraph graph("db-batch-ogre.log");

    const QString dbPath = QStringLiteral("db_batch.db");
    QFile::remove(dbPath);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();
    const bool counting = installCommitHook();
    if (!counting) printf("note: no sqlite3 commit hook — commit counts are NOT asserted\n");

    const QString projectGuid = GUIDManager::generateGUID();
    CHECK(db.createProject(projectGuid, "Db Batch"), "project created");

    const int kRows = 300;

    // -----------------------------------------------------------------------
    // 1. THE OWNER'S CASE: 300 primitive rows, with and without the batch.
    // -----------------------------------------------------------------------
    Measured loose = measure([&]{
        for (int i = 0; i < kRows; ++i) makePrimitiveRow(db, projectGuid, i);
    });
    printf("      %d rows, autocommit: %d commit(s), %.2f ms\n",
           kRows, loose.commits, loose.micros / 1000.0);
    if (counting)
        CHECK(loose.commits >= kRows,
              "without a batch, every asset row is its own transaction (>= one per row)");

    Measured batched = measure([&]{
        DbBatch batch(&db);
        for (int i = 0; i < kRows; ++i) makePrimitiveRow(db, projectGuid, 1000 + i);
    });
    printf("      %d rows, one batch:  %d commit(s), %.2f ms\n",
           kRows, batched.commits, batched.micros / 1000.0);
    if (counting)
        CHECK(batched.commits == 1, "inside ONE batch, 300 asset rows cost ONE commit");
    CHECK(batched.counted == 1, "...and Database::durableCommits() says the same");
    CHECK(assetsNamed(QStringLiteral("Cube 1")) >= kRows,
          "...and all 300 rows are in the database afterwards");

    // -----------------------------------------------------------------------
    // 2. THE SCOPE IS A REAL TRANSACTION: nothing is visible outside it until
    //    it closes. (A second connection to the same file, so the read cannot
    //    see our own uncommitted rows.)
    // -----------------------------------------------------------------------
    {
        const QString probeName = QStringLiteral("Uncommitted-");
        const QString connName = QStringLiteral("db_batch_probe");
        {
            QSqlDatabase probe = QSqlDatabase::addDatabase("QSQLITE", connName);
            probe.setDatabaseName(dbPath);
            CHECK(probe.open(), "a second connection opened onto the same file");

            auto probeCount = [&]() {
                QSqlQuery q(probe);
                q.prepare("SELECT COUNT(*) FROM assets WHERE name LIKE ?");
                q.addBindValue(probeName + QStringLiteral("%"));
                return (q.exec() && q.next()) ? q.value(0).toInt() : -1;
            };

            {
                DbBatch batch(&db);
                const QString guid = GUIDManager::generateGUID();
                db.createAssetEntry(guid, probeName + QStringLiteral("row"),
                                    static_cast<int>(ModelTypes::Object),
                                    projectGuid, projectGuid);
                CHECK(db.batchDepth() == 1, "the batch reads depth 1 while it is open");
                CHECK(probeCount() == 0,
                      "...and the row is NOT visible to another connection yet");
            }
            CHECK(db.batchDepth() == 0, "the guard closed the scope");
            CHECK(probeCount() == 1, "...and the row appeared when the batch committed");
            probe.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }

    // -----------------------------------------------------------------------
    // 3. BATCHES NEST BY COUNT — only the outermost one commits. (A verb that
    //    opens its own batch inside a script run must not commit the run's.)
    // -----------------------------------------------------------------------
    {
        Measured nested = measure([&]{
            DbBatch outer(&db);
            makePrimitiveRow(db, projectGuid, 5000);
            {
                DbBatch inner(&db);
                CHECK(db.batchDepth() == 2, "a nested batch counts up");
                makePrimitiveRow(db, projectGuid, 5001);
            }
            CHECK(db.batchDepth() == 1, "...and back down, without committing");
            makePrimitiveRow(db, projectGuid, 5002);
        });
        if (counting) CHECK(nested.commits == 1, "three rows over two nested batches: ONE commit");
    }

    // -----------------------------------------------------------------------
    // 4. THE BATCH STANDS DOWN FOR A REAL OWNER.
    //
    // The import commit unwinds a failed import with DbTransaction::rollback().
    // If the batch held the connection, that guard would degrade to a no-op and
    // the failed import's rows would ride the batch's commit instead of going
    // away. So a top-level guard opened inside a batch gets a REAL transaction,
    // and the batch takes its own back afterwards.
    // -----------------------------------------------------------------------
    {
        const QString kept = GUIDManager::generateGUID();
        const QString rolled = GUIDManager::generateGUID();
        {
            DbBatch batch(&db);
            db.createAssetEntry(kept, QStringLiteral("Stand-down kept"),
                                static_cast<int>(ModelTypes::Object), projectGuid, projectGuid);
            {
                DbTransaction tx(QSqlDatabase::database());
                CHECK(tx.isActive(),
                      "a transaction opened inside a batch is REAL, not degraded");
                CHECK(!db.batchTransactionLive(),
                      "...because the batch committed what it had and stood down");
                db.createAssetEntry(rolled, QStringLiteral("Stand-down rolled"),
                                    static_cast<int>(ModelTypes::Object), projectGuid, projectGuid);
                tx.rollback();
            }
            CHECK(db.batchDepth() == 1, "the batch SCOPE survived the nested owner");
            CHECK(db.batchTransactionLive(),
                  "...and took its transaction back when the owner let go");
        }
        CHECK(assetsNamed(QStringLiteral("Stand-down kept")) == 1,
              "the row written before the stand-down survived");
        CHECK(assetsNamed(QStringLiteral("Stand-down rolled")) == 0,
              "...and the nested owner's ROLLBACK really rolled its row back");
    }

    // -----------------------------------------------------------------------
    // 4b. A GUARD ON ANOTHER CONNECTION DOES NOT MAKE OURS "NESTED" (H2).
    //
    // The guard count is kept PER CONNECTION. When it was process-wide, a
    // transaction on a side connection — an export bundle, the catalog
    // rebuild, a .jaf version probe, all of which open their own — wrapped
    // around a main-connection guard made that guard look nested: the batch
    // would not have stood down and the guard would have degraded to the
    // no-op the design exists to prevent. A transaction on another connection
    // cannot be an outer transaction of this one.
    // -----------------------------------------------------------------------
    {
        const QString sideName = QStringLiteral("db_batch_side");
        const QString rolled = GUIDManager::generateGUID();
        {
            QSqlDatabase side = QSqlDatabase::addDatabase("QSQLITE", sideName);
            side.setDatabaseName(QStringLiteral("db_batch_side.db"));
            CHECK(side.open(), "a side connection opened (its own file, its own transactions)");
            {
                DbBatch batch(&db);
                db.createAssetEntry(GUIDManager::generateGUID(), QStringLiteral("Side-batched"),
                                    static_cast<int>(ModelTypes::Object), projectGuid, projectGuid);
                DbTransaction outerElsewhere(side);
                CHECK(outerElsewhere.isActive(), "the side connection's guard is live");
                {
                    DbTransaction ours(QSqlDatabase::database());
                    CHECK(ours.isActive(),
                          "a main-connection guard INSIDE it is still real, not degraded");
                    CHECK(!db.batchTransactionLive(),
                          "...and the batch still stood down for it");
                    db.createAssetEntry(rolled, QStringLiteral("Side-rolled"),
                                        static_cast<int>(ModelTypes::Object),
                                        projectGuid, projectGuid);
                    ours.rollback();
                }
                CHECK(db.batchTransactionLive(),
                      "...and took its transaction back when that guard let go");
            }
            CHECK(assetsNamed(QStringLiteral("Side-batched")) == 1, "the batched row survived");
            CHECK(assetsNamed(QStringLiteral("Side-rolled")) == 0,
                  "...and the nested guard's rollback still rolled ITS row back");
            side.close();
        }
        QSqlDatabase::removeDatabase(sideName);
        QFile::remove(QStringLiteral("db_batch_side.db"));
    }

    // -----------------------------------------------------------------------
    // 5. ResetMaterialCommand: one click of Reset, one commit each way.
    // -----------------------------------------------------------------------
    {
        const QString nodeGuid = GUIDManager::generateGUID();
        QStringList defaultTextures;
        for (int i = 0; i < 6; ++i) {
            const QString tex = GUIDManager::generateGUID();
            db.createAssetEntry(tex, QStringLiteral("Reset tex %1").arg(i),
                                static_cast<int>(ModelTypes::Texture), projectGuid, projectGuid);
            defaultTextures.append(tex);
        }
        // What the node uses TODAY: six edges the reset has to drop.
        QStringList oldTextures;
        for (int i = 0; i < 6; ++i) {
            const QString tex = GUIDManager::generateGUID();
            db.createAssetEntry(tex, QStringLiteral("Old tex %1").arg(i),
                                static_cast<int>(ModelTypes::Texture), projectGuid, projectGuid);
            db.createDependency(static_cast<int>(ModelTypes::Object),
                                static_cast<int>(ModelTypes::Texture), nodeGuid, tex, projectGuid);
            oldTextures.append(tex);
        }
        CHECK(edgeCount(nodeGuid) == 6, "the node starts with six texture edges");

        // THE SHAPE IT REPLACED, measured on the same disk: the identical
        // twelve edge writes with no batch. Not an assertion (it is a
        // measurement of this machine's disk), but it is what the one commit
        // below is one commit INSTEAD of.
        {
            const QString scratch = GUIDManager::generateGUID();
            const Measured loose12 = measure([&]{
                for (const QString &tex : oldTextures)
                    db.createDependency(static_cast<int>(ModelTypes::Object),
                                        static_cast<int>(ModelTypes::Texture), scratch, tex,
                                        projectGuid);
                for (const QString &tex : oldTextures) db.deleteDependency(scratch, tex);
            });
            printf("      the same 12 edge writes, autocommit: %d commit(s), %.2f ms\n",
                   loose12.commits, loose12.micros / 1000.0);
        }

        auto meshNode = iris::MeshNode::create();
        meshNode->setGUID(nodeGuid);
        meshNode->setMaterial(iris::PbrMaterial::create());

        Project project;
        project.setProjectGuid(projectGuid);

        // `newlyPinned` empty on purpose: the pin half needs the asset store,
        // and the subject here is the ROW WORK's transaction shape.
        ResetMaterialCommand reset(&db, &project, meshNode, iris::PbrMaterial::create(),
                                   defaultTextures, QStringList());

        const Measured redo = measure([&]{ reset.redo(); });
        printf("      reset redo (6 textures): %d commit(s), %.2f ms\n",
               redo.commits, redo.micros / 1000.0);
        if (counting) CHECK(redo.commits == 1, "a six-texture material Reset costs ONE commit");
        CHECK(edgeCount(nodeGuid) == 6, "...and the node now uses the six default textures");

        const Measured undo = measure([&]{ reset.undo(); });
        printf("      reset undo (6 textures): %d commit(s), %.2f ms\n",
               undo.commits, undo.micros / 1000.0);
        if (counting) CHECK(undo.commits == 1, "undoing it costs ONE commit");
        CHECK(edgeCount(nodeGuid) == 6, "...and the six original edges are back");
    }

    // -----------------------------------------------------------------------
    // 6. closeDatabase() never leaves a batch open.
    // -----------------------------------------------------------------------
    const QString survivor = GUIDManager::generateGUID();
    {
        db.beginBatch();
        db.createAssetEntry(survivor, QStringLiteral("Closed mid-batch"),
                            static_cast<int>(ModelTypes::Object), projectGuid, projectGuid);
        CHECK(db.batchDepth() == 1, "a batch is open when the close begins");
        db.closeDatabase();
        CHECK(db.batchDepth() == 0, "closeDatabase() dropped the scope");
    }
    {
        Database reopened;
        CHECK(reopened.initializeDatabase(dbPath), "database reopened to check the write");
        QSqlQuery q;
        q.prepare("SELECT COUNT(*) FROM assets WHERE guid = ?");
        q.addBindValue(survivor);
        const int found = (q.exec() && q.next()) ? q.value(0).toInt() : -1;
        CHECK(found == 1, "...and the row written inside it was COMMITTED, not rolled back");
        reopened.closeDatabase();
    }

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
