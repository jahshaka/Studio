// Threaded-import suite (the UI-freeze fix; see importbatchrunner.h).
//
// Sections:
//   1. ImportBatchRunner happy path: a real GLB (embedded texture) + a PNG
//      through ONE runner — per-stage progress observed, completion signals
//      on the main thread, catalog rows + determinism record written.
//   2. The prepare/commit split crosses threads: prepare() on a QtConcurrent
//      worker (hashes prepaid), commit() on the main thread succeeds.
//   3. Cancel mid-import: cancelling at the store stage rolls the
//      transaction back — no rows, no files rows, no orphan CAS objects.
//   4. The synchronous facade (assets.importFile's path) is unchanged.
//   5. The completion tail (the UI-freeze-on-finish fix): the threaded mesh
//      import carries the worker-parsed fragment (ImportResult::node);
//      ImportMeshTail feeds it to the viewer with NO second assimp parse
//      (AssetHelper::meshParseCount) as a deep duplicate (the session node
//      stays pristine); ImportTailQueue pumps one item per event-loop turn
//      in order, progress + finished observed — completion signals precede
//      every tail item, and the viewer/tile update hook fires per item.
#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRandomGenerator>
#include <QSemaphore>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>
#include <cstdio>

#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/assethelper.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimportservice.h"
#include "services/import/importbatchrunner.h"
#include "services/import/importtypes.h"
#include "data/constants.h"
#include "ui/pages/headlessassetviewer.h"
#include "ui/pages/importviewertail.h"
#include "irisgl/document/scenegraph/scenenode.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static int countRows(QSqlDatabase conn, const QString &sql)
{
    QSqlQuery q(conn);
    q.exec(sql);
    return q.next() ? q.value(0).toInt() : -1;
}

static int countObjects(const QString &root)
{
    int count = 0;
    QDirIterator it(root + QStringLiteral("/objects"), QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); ++count; }
    return count;
}

// A unique-content PNG (random pixels): dedup never collapses it into an
// earlier object, so store/rollback assertions see real deltas.
static QString writeUniquePng(const QString &path)
{
    QImage img(48, 48, QImage::Format_RGBA8888);
    auto *rng = QRandomGenerator::global();
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixel(x, y, rng->generate());
    img.save(path, "PNG");
    return path;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const QString cwd = QDir::currentPath();
    const QString dbPath = cwd + "/import_async.db";
    const QString root = cwd + "/import_async_store";
    QFile::remove(dbPath);
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "fresh database opened");
    db.createAllTables();
    AssetStorePaths::setRootOverride(root);
    QSqlDatabase conn = QSqlDatabase::database();

    const QString glb =
        QString(JAHSHAKA_TEST_SOURCE_DIR "/tests/importer/fixtures/mislabeled_embedded.glb");
    CHECK(QFileInfo::exists(glb), "GLB fixture present");
    const QString png1 = writeUniquePng(cwd + "/tex_batch.png");

    // ---- 1. threaded batch: GLB + PNG through one runner ------------------
    {
        ImportBatchRunner runner(&db, nullptr);
        ImportRequest meshReq;
        meshReq.sourcePath = glb;
        meshReq.typeHint = static_cast<int>(ModelTypes::Mesh);
        ImportRequest texReq;
        texReq.sourcePath = png1;
        texReq.typeHint = static_cast<int>(ModelTypes::Texture);
        runner.setRequests({ meshReq, texReq });

        int started = 0;
        QStringList stages;
        QVector<ImportResult> results;
        bool signalsOnMain = true;
        bool sawFinished = false, finishedCancelled = true;
        QEventLoop loop;

        QObject::connect(&runner, &ImportBatchRunner::fileStarted, &app,
                         [&](int, int total, const QString &) {
            ++started;
            signalsOnMain = signalsOnMain && (QThread::currentThread() == app.thread());
            if (total != 2) signalsOnMain = false;
        });
        QObject::connect(&runner, &ImportBatchRunner::stageProgress, &app,
                         [&](int, const QString &stage, int, int) {
            if (!stages.contains(stage)) stages.append(stage);
        });
        QObject::connect(&runner, &ImportBatchRunner::fileFinished, &app,
                         [&](int, const ImportRequest &, const ImportResult &result) {
            results.append(result);
            signalsOnMain = signalsOnMain && (QThread::currentThread() == app.thread());
        });
        QObject::connect(&runner, &ImportBatchRunner::finished, &app, [&](bool cancelled) {
            sawFinished = true;
            finishedCancelled = cancelled;
            loop.quit();
        });

        QTimer::singleShot(90000, &loop, &QEventLoop::quit);   // watchdog
        runner.start();
        loop.exec();

        CHECK(sawFinished, "batch finished (no watchdog timeout)");
        CHECK(!finishedCancelled, "finished(cancelled=false) for a clean batch");
        CHECK(started == 2, "fileStarted for each of the 2 files (N of M)");
        CHECK(results.size() == 2 && results[0].ok() && results[1].ok(),
              "both files imported ok");
        CHECK(signalsOnMain, "fileStarted/fileFinished delivered on the UI thread");
        CHECK(stages.contains("convert") && stages.contains("hash") && stages.contains("store"),
              "per-stage progress observed (convert/hash/store)");

        // The GLB plan: Object + Mesh member + embedded Texture member rows.
        CHECK(countRows(conn, "SELECT COUNT(*) FROM assets") >= 4,
              "catalog rows exist for both imports (object+mesh+texture, image)");
        CHECK(countRows(conn, "SELECT COUNT(*) FROM asset_files") >= 3,
              "asset_files rows recorded");
        CHECK(countObjects(root) >= 2, "CAS objects stored");

        AssetImportService service(&db, nullptr);
        const QJsonObject record = service.importSettings(results[0].assetGuid);
        CHECK(record.value("importer").toString() == QStringLiteral("mesh")
                  && !record.value("sourceOid").toString().isEmpty(),
              "determinism record (importer + sourceOid) written by the threaded path");
    }

    // ---- 2. prepare on a worker thread, commit on the main thread ---------
    {
        const QString png2 = writeUniquePng(cwd + "/tex_split.png");
        AssetImportService service(&db, nullptr);
        ImportRequest request;
        request.sourcePath = png2;
        request.typeHint = static_cast<int>(ModelTypes::Texture);

        PreparedImport prepared;
        QThread *workerThread = nullptr;
        QSemaphore startedSem;   // waitForFinished may steal a NOT-YET-STARTED
                                 // task and run it inline — force a real start
        QFuture<void> future = QtConcurrent::run([&]() {
            startedSem.release();
            workerThread = QThread::currentThread();
            prepared = service.prepare(request);
        });
        startedSem.acquire();
        future.waitForFinished();

        CHECK(workerThread && workerThread != app.thread(),
              "prepare executed off the main thread");
        CHECK(prepared.ok(), "worker-thread prepare produced a staged plan");
        CHECK(!prepared.staged.fileOids.isEmpty(),
              "content hashes prepaid on the worker (StagedAsset::fileOids)");

        const ImportResult result = service.commit(prepared);
        CHECK(result.ok(), "main-thread commit of the worker-prepared plan succeeded");
        CHECK(!result.objectOids.isEmpty(), "commit recorded the stored objects");
    }

    // ---- 3. cancel mid-import rolls back ----------------------------------
    {
        const int assetsBefore = countRows(conn, "SELECT COUNT(*) FROM assets");
        const int filesBefore = countRows(conn, "SELECT COUNT(*) FROM files");
        const int linksBefore = countRows(conn, "SELECT COUNT(*) FROM asset_files");
        const int objectsBefore = countObjects(root);

        const QString png3 = writeUniquePng(cwd + "/tex_cancel.png");
        ImportBatchRunner runner(&db, nullptr);
        ImportRequest request;
        request.sourcePath = png3;
        request.typeHint = static_cast<int>(ModelTypes::Texture);
        runner.setRequests({ request });

        ImportResult cancelResult;
        bool finishedCancelled = false;
        QEventLoop loop;
        QObject::connect(&runner, &ImportBatchRunner::stageProgress, &app,
                         [&](int, const QString &stage, int, int) {
            // The dialog's Cancel, arriving while the store stage reports.
            if (stage == QStringLiteral("store")) runner.cancel();
        });
        QObject::connect(&runner, &ImportBatchRunner::fileFinished, &app,
                         [&](int, const ImportRequest &, const ImportResult &result) {
            cancelResult = result;
        });
        QObject::connect(&runner, &ImportBatchRunner::finished, &app, [&](bool cancelled) {
            finishedCancelled = cancelled;
            loop.quit();
        });
        QTimer::singleShot(60000, &loop, &QEventLoop::quit);
        runner.start();
        loop.exec();

        CHECK(finishedCancelled, "finished(cancelled=true) after Cancel");
        CHECK(cancelResult.error == QStringLiteral("cancelled"),
              "the cancelled file reports 'cancelled'");
        CHECK(countRows(conn, "SELECT COUNT(*) FROM assets") == assetsBefore,
              "cancel: no catalog rows survive the rollback");
        CHECK(countRows(conn, "SELECT COUNT(*) FROM files") == filesBefore,
              "cancel: no files rows survive the rollback");
        CHECK(countRows(conn, "SELECT COUNT(*) FROM asset_files") == linksBefore,
              "cancel: no asset_files rows survive the rollback");
        CHECK(countObjects(root) == objectsBefore,
              "cancel: no orphan CAS objects left in the store");
    }

    // ---- 4. the synchronous facade (verb path) is unchanged ---------------
    {
        const QString png4 = writeUniquePng(cwd + "/tex_sync.png");
        AssetImportService service(&db, nullptr);
        ImportRequest request;
        request.sourcePath = png4;
        request.typeHint = static_cast<int>(ModelTypes::Texture);
        const ImportResult result = service.import(request);
        CHECK(result.ok(), "synchronous dialog-free import (assets.importFile path) works");
    }

    // ---- 5. the completion tail: prepared node, no second parse, queued ---
    {
        // Probe viewer: records how the tail feeds it (the "tile update"
        // seam AssetView hangs its overlay/pixmap refresh on).
        class ProbeViewer : public HeadlessAssetViewer
        {
        public:
            int addNodeCalls = 0;
            int loadModelCalls = 0;
            iris::SceneNodePtr lastAdded;
            void addNodeToScene(iris::SceneNodePtr sceneNode, QString guid, bool viewed,
                                bool cache, bool isOnGround) override
            {
                ++addNodeCalls;
                lastAdded = sceneNode;
                HeadlessAssetViewer::addNodeToScene(sceneNode, guid, viewed, cache, isOnGround);
            }
            void loadModel(QString path, QString guid, bool firstAdd, bool cache,
                           bool firstLoad) override
            {
                ++loadModelCalls;
                HeadlessAssetViewer::loadModel(path, guid, firstAdd, cache, firstLoad);
            }
        };

        const int parsesBefore = AssetHelper::meshParseCount();

        ImportBatchRunner runner(&db, nullptr);
        ImportRequest meshReq;
        meshReq.sourcePath = glb;
        meshReq.typeHint = static_cast<int>(ModelTypes::Mesh);
        runner.setRequests({ meshReq });

        QVector<ImportResult> results;
        QStringList sequence;
        QEventLoop loop;
        QObject::connect(&runner, &ImportBatchRunner::fileFinished, &app,
                         [&](int, const ImportRequest &, const ImportResult &result) {
            results.append(result);
            sequence.append(QStringLiteral("fileFinished"));
        });
        QObject::connect(&runner, &ImportBatchRunner::finished, &app, [&](bool) {
            sequence.append(QStringLiteral("batchFinished"));
            loop.quit();
        });
        QTimer::singleShot(90000, &loop, &QEventLoop::quit);
        runner.start();
        loop.exec();

        CHECK(results.size() == 1 && results[0].ok(), "tail-section mesh import ok");
        CHECK(sequence == QStringList({ "fileFinished", "batchFinished" }),
              "completion order: fileFinished precedes finished, tail not yet run");
        CHECK(!results[0].node.isNull(),
              "the threaded mesh import carries the worker-parsed fragment (tail input)");
        CHECK(AssetHelper::meshParseCount() == parsesBefore + 1,
              "the pipeline parsed the model exactly once (convert)");

        // The tail through the queue: one item per event-loop turn.
        ProbeViewer probe;
        ImportTailQueue queue;
        ImportMeshTail::Outcome outcome;
        QVector<int> order;
        int progressTicks = 0;
        bool tailFinished = false;
        QEventLoop tailLoop;
        QObject::connect(&queue, &ImportTailQueue::progress, &app,
                         [&](int, int) { ++progressTicks; });
        QObject::connect(&queue, &ImportTailQueue::finished, &app, [&]() {
            tailFinished = true;
            tailLoop.quit();
        });

        const ImportResult meshResult = results[0];
        queue.enqueue([&]() {
            outcome = ImportMeshTail::run(&db, &probe, meshResult, glb);
            order.append(0);
            // Posted DURING item 0: with per-turn pumping this queued call
            // runs BETWEEN item 0 and item 1; a synchronous loop would run
            // it only after the whole queue drained.
            QTimer::singleShot(0, &app, [&]() { order.append(200); });
        });
        queue.enqueue([&]() { order.append(1); });
        QTimer::singleShot(30000, &tailLoop, &QEventLoop::quit);
        queue.start();
        tailLoop.exec();

        CHECK(tailFinished, "tail queue drained and reported finished");
        CHECK(progressTicks == 2, "tail queue reported per-item progress");
        CHECK(order == QVector<int>({ 0, 200, 1 }),
              "tail items run one per event-loop turn (the loop breathes between items)");
        CHECK(outcome.usedPreparedNode,
              "the tail consumed the prepared fragment (no reader load)");
        CHECK(probe.addNodeCalls == 1 && probe.loadModelCalls == 0,
              "viewer got the node directly; the re-parsing loadModel path never ran");
        CHECK(AssetHelper::meshParseCount() == parsesBefore + 1,
              "the tail added NO second assimp parse");
        CHECK(!probe.lastAdded.isNull() && probe.lastAdded != meshResult.node,
              "the viewer got a deep duplicate — the session-registered node stays pristine");
        CHECK(probe.cachedAsset(meshResult.assetGuid) == probe.lastAdded,
              "the tail cached the preview node under the asset guid (tile reselects hit it)");

        // The row survived the tail's property merge with its determinism
        // record intact (no clobber of the "import"/"metadata" blocks).
        AssetImportService service(&db, nullptr);
        CHECK(service.importSettings(meshResult.assetGuid)
                  .value("importer").toString() == QStringLiteral("mesh"),
              "tail property merge kept the import record");
    }

    std::printf(failures ? "FAILED: %d check(s)\n" : "ALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
