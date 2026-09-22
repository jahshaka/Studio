// commands.undo_clear_cost — CLOSE-1: CLEARING AN UNDO STACK COSTS MEMORY,
// NOT DISK SYNCS.
//
// THE INCIDENT (owner session, 2026-09-14). Closing the project from the
// desktop grid froze the window for 33,156 ms. The watchdog's backtrace named
// every frame of it:
//
//     ItemGridWidget::closeProject -> ProjectManager::closeProject
//       -> MainWindow::closeProject -> UndoService::clear
//       -> QUndoStack::clear -> ~QUndoCommand
//       -> DeleteSceneNodeCommand::~DeleteSceneNodeCommand
//       -> Database::deleteAsset -> Database::deleteAssetRow
//       -> DbTransaction::commit -> sqlite3PagerCommitPhaseOne -> fdatasync
//
// One transaction and one fdatasync PER COMMAND DESTRUCTOR, and the stack held
// hundreds of delete commands after a session of scripted spheres. The cause is
// not SQLite and not the undo stack: it is a DESTRUCTOR DOING SYNCHRONOUS DISK
// I/O ONE ROW AT A TIME on the UI thread.
//
// The fix and what this suite pins:
//   1. The destructor ENQUEUES (Database::enqueueAssetDelete) — it writes
//      nothing, commits nothing, syncs nothing.
//   2. One flush applies the whole queue inside ONE transaction, so a clear of
//      N delete commands costs ONE commit however large N is.
//   3. UndoService::clear() is that flush point: the rows are really gone
//      afterwards, and the queue is empty.
//   4. A closed connection or an outer transaction never loses a queued row.
//
// The COMMIT COUNT is the honest measure (a wall-clock bound is a measure of
// the disk, not of the code), and it comes from sqlite3_commit_hook on the
// handle Qt's own QSQLITE driver opened — no instrumentation in the production
// path, the tests/assettray idiom.
//
// THE TIMING ASSERTION IS A RATIO, NOT A MILLISECOND COUNT (CLOSE-2). The
// brief's absolute 50 ms bound was a measurement of the disk and of the box's
// load: the same binary cleared 300 commands in 21 ms, 29 ms, 64 ms and 90 ms
// on four runs of one afternoon, so it redded under a -j4 gate and once even
// solo, with the commit count — the actual contract — reading 1 every time.
// Both shapes are measured in the SAME run on the SAME disk and the clear must
// be at least 1.5x cheaper than the one it replaced (it measures 1.9x-2.0x since
// the library went to WAL, and a regression to per-destructor commits measures
// 1x — see the factor's note). That
// catches a regression to per-destructor commits, which is what the bound was
// for, and cannot be made to fail by a busy machine. On a build dir that lives
// on tmpfs, where a sync costs nothing, the old shape falls below a 5 ms floor
// and the ratio is printed instead of asserted — a ratio between two numbers
// that are both noise proves nothing either way.
//
// Framework-free (printf + a failure counter), offscreen, DISPLAY-FREE.
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlDriver>
#include <QSqlQuery>
#include <QUndoStack>
#include <QVariant>
#include <cstdio>

#include "commands/deletescenenodecommand.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/undoservice.h"

#include "../support/documentgraph.h"

#ifdef JAH_HAVE_SQLITE3
#include <sqlite3.h>
#endif

static int failures = 0;
static int checks = 0;
#define CHECK(cond, msg) do { ++checks; if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

// ---------------------------------------------------------------------------
// The COMMIT counter. Every commit on this connection is one
// sqlite3PagerCommitPhaseOne, i.e. one fdatasync — which is exactly the unit
// the owner's 33 s was spent in.
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

struct Measured { int commits = 0; qint64 micros = 0; };

template <typename Fn>
Measured measure(Fn fn)
{
    Measured m;
    QElapsedTimer timer;
    gCommits = 0;
    timer.start();
    fn();
    m.micros = timer.nsecsElapsed() / 1000;
    m.commits = gCommits;
    return m;
}

int assetRowCount(const QStringList &guids)
{
    int found = 0;
    for (const QString &guid : guids) {
        QSqlQuery q;
        q.prepare("SELECT COUNT(*) FROM assets WHERE guid = ?");
        q.addBindValue(guid);
        if (q.exec() && q.next()) found += q.value(0).toInt();
    }
    return found;
}

/// A built-in primitive exactly as SceneEditService::addPrimitive makes
/// one: a mesh node whose GUID names an Object row in the library.
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

/// The command as the editor pushes it: a node in a scene, already redone
/// (the delete performed), so its destructor is the one that finalises the row.
DeleteSceneNodeCommand *deletedNodeCommand(Database *db, const iris::SceneNodePtr &root,
                                           const QString &guid)
{
    auto node = iris::MeshNode::create();
    node->setName(QStringLiteral("Cube"));
    node->setGUID(guid);
    node->isBuiltIn = true;
    root->addChild(node);
    auto *cmd = new DeleteSceneNodeCommand(root, node, db, guid);
    cmd->redo();     // the delete itself — this is what makes the dtor act
    return cmd;
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets (QMessageBox)
    enginetest::DocumentGraph graph("undo-clear-cost-ogre.log");  // a document node IS an engine node

    const QString dbPath = QStringLiteral("undo_clear_cost.db");
    QFile::remove(dbPath);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();
    const bool counting = installCommitHook();
    if (!counting) printf("note: no sqlite3 commit hook — commit counts are NOT asserted\n");

    const QString projectGuid = GUIDManager::generateGUID();
    CHECK(db.createProject(projectGuid, "Undo Clear Cost"), "project created");

    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();

    // -----------------------------------------------------------------------
    // 1. THE DESTRUCTOR WRITES NOTHING.
    // -----------------------------------------------------------------------
    {
        const QString guid = makePrimitiveRow(db, projectGuid, 0);
        auto *cmd = deletedNodeCommand(&db, root, guid);

        const Measured death = measure([&]{ delete cmd; });
        if (counting)
            CHECK(death.commits == 0, "a delete command's DESTRUCTOR commits NOTHING");
        CHECK(assetRowCount({ guid }) == 1, "...the asset row is still there");
        CHECK(db.pendingAssetDeleteCount() == 1, "...and the row is QUEUED instead");

        const Measured flush = measure([&]{ db.flushPendingAssetDeletes(); });
        if (counting) CHECK(flush.commits == 1, "the flush is ONE transaction");
        CHECK(assetRowCount({ guid }) == 0, "...and the row is gone after it");
        CHECK(db.pendingAssetDeleteCount() == 0, "...queue empty");
    }

    // -----------------------------------------------------------------------
    // 2. THE OWNER'S CASE: 300 delete commands, cleared.
    //
    // The real UndoService, the real QUndoStack, the real commands, the real
    // Database — this IS the stack in the watchdog backtrace, minus the widget
    // that started it.
    // -----------------------------------------------------------------------
    const int kCommands = 300;
    qint64 clearMicros = 0;   // section 2's measurement, compared in section 3
    {
        QUndoStack stack;
        UndoService undo(&stack);
        undo.setDeferredFlushHook([&]{ db.flushPendingAssetDeletes(); });

        QStringList guids;
        for (int i = 0; i < kCommands; ++i) {
            const QString guid = makePrimitiveRow(db, projectGuid, i + 1);
            guids.append(guid);
            // push() runs redo() itself, exactly like the editor's delete.
            auto node = iris::MeshNode::create();
            node->setGUID(guid);
            node->isBuiltIn = true;
            root->addChild(node);
            undo.push(new DeleteSceneNodeCommand(root, node, &db, guid));
        }
        CHECK(stack.count() == kCommands, "300 delete commands on the stack");
        CHECK(assetRowCount(guids) == kCommands, "...and 300 asset rows to finalise");
        CHECK(db.pendingAssetDeleteCount() == 0,
              "...nothing queued yet: the commands are alive and undoable");

        const Measured cleared = measure([&]{ undo.clear(); });
        clearMicros = cleared.micros;
        printf("info: UndoService::clear() of %d delete commands: %lld us, %d commit(s)\n",
               kCommands, static_cast<long long>(cleared.micros), cleared.commits);

        if (counting)
            CHECK(cleared.commits == 1,
                  "clearing 300 delete commands is ONE commit (was 300: one per destructor)");
        CHECK(assetRowCount(guids) == 0, "...every queued row was really deleted");
        CHECK(db.pendingAssetDeleteCount() == 0, "...and the queue is empty afterwards");
        CHECK(stack.count() == 0, "...the stack is clear");
    }

    // -----------------------------------------------------------------------
    // 3. THE SHAPE THAT FROZE THE WINDOW, measured on this very box for the
    //    record: the same 300 deletes written ONE AT A TIME.
    // -----------------------------------------------------------------------
    {
        QStringList guids;
        for (int i = 0; i < kCommands; ++i) guids.append(makePrimitiveRow(db, projectGuid, 1000 + i));

        const Measured direct = measure([&]{
            for (const QString &guid : guids) db.deleteAsset(guid);
        });
        printf("info: the OLD shape — %d immediate deleteAsset() calls: %lld us, %d commit(s)\n",
               kCommands, static_cast<long long>(direct.micros), direct.commits);
        if (counting)
            CHECK(direct.commits == kCommands,
                  "an immediate delete is one commit EACH — the cost the queue removes");
        CHECK(assetRowCount(guids) == 0, "...they are deleted either way");
        // The wall clock, as a RATIO against the shape it replaced — same disk,
        // same run, same load (see the header). The owner's freeze was 33,156 ms
        // of exactly this.
        printf("info: the clear is %.1fx cheaper than the old shape\n",
               clearMicros > 0 ? double(direct.micros) / double(clearMicros) : 0.0);
        // A FLOOR UNDER THE COMPARISON. On a build directory that lives on
        // tmpfs an fdatasync costs nothing at all, so 300 of them can come in
        // under 5 ms and the ratio stops being a measurement of anything — it
        // would red with the commit counts, which ARE the contract, reading 1
        // and 300. Say so and move on; the commit assertions above still hold.
        if (direct.micros < 5000) {
            printf("info: the old shape took %lld us — under the 5 ms floor "
                   "(a tmpfs build dir?), the ratio is NOT asserted\n",
                   static_cast<long long>(direct.micros));
        } else {
            // THE FACTOR IS 1.5, AND THE REASON IS WHAT THE TEST HAS TO
            // DISTINGUISH. A regression to one commit per destructor makes the
            // two shapes the SAME shape — a ratio of about 1.
            //
            // IT USED TO BE 3, when it measured 32x-52x solo, and FSYNC-2 moved
            // it: the library now opens in WAL at synchronous=NORMAL
            // (database.cpp), so a commit no longer waits for the device and
            // the 300 extra transactions the old shape pays are 300 lots of
            // TRANSACTION OVERHEAD instead of 300 fdatasyncs. That is the same
            // collapse the tmpfs floor above describes, arriving by a different
            // route, and it is not a regression in the thing this suite guards:
            // the CONTRACT is the commit COUNT, asserted exactly above (1
            // against 300, from sqlite3_commit_hook), and it still reads 1.
            // Measured after the move, five solo runs: 18.3-19.2 ms against
            // 35.5-37.7 ms, i.e. 1.9x-2.0x every time — stable, because both
            // halves are now CPU in one process rather than one half being the
            // disk. 1.5 sits comfortably between that and the 1.0 a regression
            // would read.
            CHECK(clearMicros > 0 && double(direct.micros) > double(clearMicros) * 1.5,
                  "clearing the stack is at least 1.5x cheaper than one delete per command");
        }
    }

    // -----------------------------------------------------------------------
    // 4. NOTHING IS LOST WHEN THE FLUSH CANNOT RUN.
    // -----------------------------------------------------------------------
    {
        Database closed;   // never initialized: no connection at all
        closed.enqueueAssetDelete(QStringLiteral("some-guid"));
        CHECK(closed.flushPendingAssetDeletes() == 0,
              "a flush on a closed connection reports nothing done");
        CHECK(closed.pendingAssetDeleteCount() == 1,
              "...and KEEPS the queued row (a lost row outlives its node for ever)");
    }

    // -----------------------------------------------------------------------
    // 5. A COMMAND THAT WAS UNDONE OWES NOTHING (unchanged semantics: the row
    //    belongs to a node that is back in the scene).
    // -----------------------------------------------------------------------
    {
        const QString guid = makePrimitiveRow(db, projectGuid, 2000);
        auto *cmd = deletedNodeCommand(&db, root, guid);
        cmd->undo();
        delete cmd;
        CHECK(db.pendingAssetDeleteCount() == 0, "an UNDONE delete queues nothing");
        CHECK(assetRowCount({ guid }) == 1, "...and its asset row survives");
    }

    // -----------------------------------------------------------------------
    // 6. closeDatabase() is the last flush point — every exit path runs it
    //    while the connection is still open (~MainWindow drains the stack).
    // -----------------------------------------------------------------------
    QString lastGuid;
    {
        lastGuid = makePrimitiveRow(db, projectGuid, 3000);
        auto *cmd = deletedNodeCommand(&db, root, lastGuid);
        delete cmd;
        CHECK(db.pendingAssetDeleteCount() == 1, "one row queued at shutdown");
        db.closeDatabase();
        CHECK(db.pendingAssetDeleteCount() == 0, "closeDatabase() flushed it");
    }
    {
        Database reopened;
        CHECK(reopened.initializeDatabase(dbPath), "database reopened to check the write");
        CHECK(assetRowCount({ lastGuid }) == 0, "...the row really went at close");
        reopened.closeDatabase();
    }

    printf("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
