/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DATABASE_H
#define DATABASE_H

#include <QHash>
#include <QSet>
#include <QSqlDatabase>   // stage 3's side connections (openPreviewConnection, writePreview) and the borrowed default
#include <QJsonArray>
#include <QCryptographicHash>

#include <functional>
#include <memory>
#include <optional>

#include "data/assethomekind.h"
#include "data/project.h"
#include "data/database/databasereads.h"
#include "data/database/dbqt.h"
#include "data/db/connection.h"
#include "data/db/transaction.h"

#include "irisgl/irisglfwd.h"

class QImage;

// RAII transaction over one db::Connection (ASSET_PIPELINE_SPEC
// phase 0): begin on construction, roll back on destruction unless commit()
// was called — so every early-return path inside a multi-statement DB
// operation unwinds cleanly instead of leaving half the rows behind (and a
// .jaf import stops paying one fsync per row). A guard NESTED inside another on the
// same connection is a no-op that rides the outer one; the OUTERMOST begins IMMEDIATE, and a
// BEGIN that fails is reported by its commit() (see the constructor).
class DbTransaction
{
public:
    // A db::Transaction on the caller's connection (NOQT-DATABASE-1 stage 2): nesting, the refusal and
    // the lock's holder are the connection's, whoever asks.
    explicit DbTransaction(db::Connection &connection) : conn(connection)
    {
        // THE BATCH STANDS DOWN FOR A REAL OWNER (CLOSE-2 item 1).
        //
        // Database::beginBatch keeps a transaction open for a whole gesture so
        // the gesture costs ONE commit. That transaction must never become the
        // "already in one" that silently degrades somebody else's guard: the
        // import commit (services/import/assetimportservice.cpp) unwinds a
        // failed import through tx.rollback(), and a degraded guard turns that
        // rollback into a no-op — the failed import's rows would then ride the
        // batch's commit. So the batch on THIS connection commits what it has and
        // stands down first; it begins again at its next write. The batch is never
        // live while a guard is (it begins only with none on its connection), so
        // for a nested guard the hook finds nothing to do.
        // OUTERMOST IS PER CONNECTION (CLOSE-2 round 2, H2): a transaction on
        // another connection cannot be an outer transaction of this one - the
        // connection's own guard count says it (the shared state).
        if (sBatchYield) sBatchYield(conn);
        outermost = conn.guards() == 0;
        // IMMEDIATE (PREVIEWS-1): the write lock is taken at the BEGIN, so no other connection (the
        // preview writer's) can commit inside this transaction and its snapshot can never go stale
        // - a DEFERRED BEGIN whose first statement is a read failed its first write with 517
        // SQLITE_BUSY_SNAPSHOT once another connection had committed (spikes/previews-1/stage1/
        // probe.out, cases 4-5). The BEGIN waits for another writer's one row at most (the busy
        // timeout). A BEGIN THAT FAILS WRITES NOTHING: the scope REFUSES every write on this
        // connection until it ends - SQLite's query_only on the connection (a raw exec() fails
        // too) and Database::executeAndCheckQuery returning false (unexecuted on a Database's own
        // connection, the one its funnel walks; a side connection's write runs and fails in SQLite,
        // query_only, still refused but logged as a failure) - it still counts as
        // the outermost guard (a nested one rides it and never begins a sub-unit), it is logged
        // ONCE here with the lock's holder, and commit() answers false. Reads still run.
        tx.emplace(conn, "a DbTransaction unit");
        if (!outermost) return;
        ++sActiveTotal;
        counted = true;
        const db::Error &begin = tx->beginError();
        if (!begin) return;
        beginError = begin.code == 517   // SQLITE_BUSY_SNAPSHOT
            ? QStringLiteral("a read left active on this connection holds a stale snapshot (%1)").arg(dbqt::describe(begin))
            : dbqt::describe(begin);
        qWarning("DbTransaction: the transaction on '%s' could NOT BEGIN: %s - every write on it is REFUSED "
                 "until this scope ends, and its commit() answers false", conn.name().c_str(),
                 qUtf8Printable(beginError));
    }

    ~DbTransaction()
    {
        tx.reset();   // a unit never committed rolls back (the layer's guard)
        release();
    }

    // On a FAILED commit the guard rolls back itself, so the connection is
    // never left mid-transaction and a caller's post-failure cleanup reads
    // COMMITTED state (the import rollback depended on that and never got
    // it — deep audit 2026-09, area 6).
    bool commit()
    {
        if (!outermost) return true;   // nested: the outer guard's commit is the unit's
        if (!tx) return false;
        db::Error error;
        const bool ok = tx->commit(&error);   // a refusing unit answers false: it never began
        if (ok) noteCommit();
        tx.reset();
        release();
        return ok;
    }

    // Explicit early rollback: ends the transaction NOW instead of at
    // destruction. The one caller that needs it is failure cleanup that has to
    // query the database — inside an open transaction such a query sees the
    // caller's own uncommitted rows, which is exactly how the failed-import
    // object cleanup became a no-op. Idempotent; a no-op on a degraded guard.
    void rollback()
    {
        if (!outermost || !tx) return;
        tx.reset();
        release();
    }

    /// True when this guard is the OUTERMOST on its connection: it owns the unit, and its
    /// commit() is the unit's answer (false when nested: the caller's commit is). An outermost guard
    /// whose BEGIN failed still answers true here, so its owner reaches commit() and hears the failure.
    bool isActive() const { return outermost; }
    /// Why the BEGIN failed; empty when it did not.
    QString beginFailure() const { return beginError; }

    DbTransaction(const DbTransaction &) = delete;
    DbTransaction &operator=(const DbTransaction &) = delete;

    // ---- the batch hook and the sync accounting (CLOSE-2) ------------------

    /// Installed by Database's constructor. Called with the connection a new
    /// guard is about to open a transaction on, so an open batch on that same
    /// connection can commit and stand down (see the constructor).
    /// A std::function rather than a call into Database because this class is
    /// header-only and is instantiated in translation units that do not link
    /// database.cpp.
    using BatchYieldHook = std::function<void(const db::Connection &)>;
    static void setBatchYieldHook(BatchYieldHook hook) { sBatchYield = std::move(hook); }
    /// Is a gesture batch holding a transaction right now? Process-wide, for
    /// the durable-commit accounting only.
    static bool anyBatchLive() { return sBatchesLive > 0; }
    static void noteBatchLive(bool live) { sBatchesLive += live ? 1 : -1; }

    /// Is an outermost guard live on any connection? For the durable-commit accounting only
    /// (Database::executeAndCheckQuery, which must not touch a member — see there). Deliberately
    /// process-wide: a diagnostic that under-counts a write made on one connection while another
    /// holds a transaction is a better trade than a member read in that funnel.
    static bool anyGuardLive() { return sActiveTotal > 0; }

    /// DURABLE COMMITS — the diagnostic behind `editor.undoState().dbCommits`.
    ///
    /// One count per commit that reached the disk: every successful
    /// DbTransaction::commit(), every Database batch commit, and every WRITE
    /// statement that ran with no transaction open at all (SQLite autocommits
    /// it — one transaction, one fdatasync). It is what makes "one gesture,
    /// one commit" an assertion instead of a stopwatch reading. Process-wide
    /// and approximate by design: it counts what goes through this layer, so a
    /// raw QSqlQuery::exec() somewhere else is invisible to it.
    static quint64 commitCount() { return sCommits; }
    static void noteCommit() { ++sCommits; }

private:
    void release()
    {
        if (!counted) return;
        counted = false;
        --sActiveTotal;
    }

    db::Connection &conn;                 ///< the caller's: it outlives this guard
    std::optional<db::Transaction> tx;    ///< the unit; reset at its commit or rollback
    bool outermost = false;
    bool counted = false;                 ///< this outermost guard is in sActiveTotal
    QString beginError;

    inline static int sActiveTotal = 0;
    inline static int sBatchesLive = 0;
    inline static quint64 sCommits = 0;
    inline static BatchYieldHook sBatchYield = BatchYieldHook();
};

/// THE FUNNEL's CURSOR (NOQT-DATABASE-1 stage 2, the lead's ruling A): ONE statement of the Database's, run by
/// Database::executeAndCheckQuery — which takes its FIRST step and applies the funnel's laws there (the query
/// log, the refusal in a failed unit, the batch at the first write, the sync accounting, a failure said by
/// name) — and read by next(), which hands that step's row first. Std only, and no Qt look-alike: its binds
/// and column reads are the layer's, by kind (db::Statement's; the Qt kinds through dbqt.h).
/// A FAILED READ IS NEVER A SHORT RESULT: a step that fails, the first or a later one, is said by the
/// statement's name and leaves failed() true; a write, a count and an "is there a row" read ask it.
/// IT CANNOT OUTLIVE WHAT IT WAS OPENED IN: once its connection is reset or closed, or the transaction it
/// was opened inside (the connection's guard depth at its construction) has ended, its next step is REFUSED
/// by name and the cursor is FAILED.
class DbQuery : public db::Statement
{
public:
    DbQuery(db::Connection &connection, std::string_view sql);

    /// The next row (the funnel's first step's, then one step each); false at the end and when FAILED.
    bool next();
    bool failed() const { return mFailed; }
    /// Why it failed (code 0 while it has not).
    const db::Error &error() const { return mError; }
    const std::string &sql() const { return mSql; }
    db::Connection &connection() const { return mConnection; }

private:
    friend class Database;
    /// The funnel's first step, said under `name` when it fails.
    bool start(std::string name);
    /// One step: a row, the end, or FAILED (said by name).
    bool advance();
    /// FAILED without a step: `e` said by name.
    void fail(const db::Error &e);
    using db::Statement::step;    // the cursor's steps are the funnel's: start() and next()
    using db::Statement::reset;

    db::Connection &mConnection;
    const db::Connection::State *mOpenedOn;   ///< the connection's state at construction
    int mOpenedAtDepth;                       ///< its transaction guards at construction
    std::string mSql;
    std::string mName;
    bool mStarted = false;
    bool mPending = false;   ///< start() found a row next() has not handed out
    bool mDone = false;
    bool mFailed = false;
    db::Error mError;
};

// Every project-scoped function takes the project guid explicitly — the data
// layer no longer reads the app's ambient current project (SCRIPTING_SPEC
// §1.6.1 / APP_ARCHITECTURE_AUDIT §2.5; callers pass
// Globals::project->getProjectGuid() until the hub dissolves).
class Database
{
public:
    Database();
    ~Database();

    /// THE FUNNEL: every statement the Database runs on its own connection (and the archive reads of an
    /// import) passes here once, before its rows are read (DbQuery). False when it was refused or failed.
    bool executeAndCheckQuery(DbQuery &query, const QString &name);

    // ---- THE QUERY LOG (D11-LIBRARY-SCALE) ----------------------------------
    // A diagnostic, OFF by default and statics only (the funnel's rule): every
    // statement through executeAndCheckQuery — AND ONLY THOSE: the catalog's raw
    // db::Statement reads are invisible to it (none of them is a listing a panel
    // reads) — counted by name, and every one whose column list
    // SELECTS A THUMBNAIL recorded with whether it is keyed by guid
    // (`guid = ?` / `guid IN (...)`). `app.queryLog` reads it; the proof that no
    // listing carries a thumbnail BLOB is "every thumbnail select is by guid".
    struct QueryLogEntry
    {
        QString name;
        QString sql;
        int count = 0;
        bool selectsThumbnail = false;
        bool byGuid = false;
    };
    static void setQueryLog(bool on);   // turning it on resets it
    static bool queryLogOn();
    static QVector<QueryLogEntry> queryLogEntries();
    static int queryLogStatements();
    /// The classifier, public for the suites: does `sql` select a thumbnail
    /// column, and is its WHERE keyed by guid?
    static void classifyQuery(const QString &sql, bool *selectsThumbnail, bool *byGuid);

    /// A thumbnail BLOB of an asset was written (updateAssetThumbnail): the shell
    /// drops that guid's cached tile (ui/controls/tilecache.h). Statics only.
    using GuidHook = std::function<void(const QString &guid)>;
    static void setAssetThumbnailWritten(GuidHook hook);

    // MANAGE ===============================================================================
    bool initializeDatabase(const QString &pathToBlob);
    /// connection() on the open handle (`driverHandle`: the Qt driver's handle()); false with a named
    /// error - and initializeDatabase then refuses to open the library - where it cannot be borrowed.
    bool borrowConnection(const QVariant &driverHandle, QString *errorOut);
    void closeDatabase();

    // ---- ONE GESTURE, ONE COMMIT (CLOSE-2 item 1) -------------------------
    //
    // SQLite autocommits every statement that runs outside a transaction, and
    // an autocommit is a real transaction: journal, write, fdatasync, journal
    // unlink. Adding a built-in primitive writes an asset row
    // (SceneEditService::addPrimitive -> createAssetEntry), so a script
    // that adds 300 of them used to pay 300+ of them one at a time, on the UI
    // thread, interleaved with the engine work.
    //
    // beginBatch()/endBatch() are a COUNTED scope over one transaction, so
    // nested users compose (the outermost pair owns the commit). The editor
    // opens one for the lifetime of a SCRIPT RUN — the run is already one undo
    // macro, which is exactly the "one gesture" boundary — and single-gesture
    // paths (the Add menu, a drag-drop: one row) stay autocommit, where one
    // commit is the right answer anyway.
    //
    // WHAT A BATCH DOES NOT DO: it never holds somebody else's transaction
    // hostage. A top-level DbTransaction opened while a batch is live makes
    // the batch commit what it has and stand down (DbTransaction's
    // constructor); the batch begins again at this Database's next WRITE
    // (never at a read: an IMMEDIATE batch holds the write lock). So an import inside a script run still owns a real
    // transaction it can roll back.
    //
    // FAILURE POLICY: a run that ends with the batch still open commits — the
    // rows describe nodes that exist. A run that throws commits too, for the
    // same reason. Only a hard kill loses them, and then the rows describe
    // nodes that died with the process.
    void beginBatch();
    /// Closes one level. The outermost close commits; the return value is that
    /// commit's success (true for an inner level, which commits nothing, and
    /// true for a level the database ABANDONED under the caller — see
    /// closeDatabase).
    bool endBatch();
    /// Commits what the open batch holds NOW, the scope left open (a no-op with none live) - what a top-level
    /// guard's stand-down does, but with its answer: false = the commit failed and the batch's rows were rolled
    /// back (announced). The scene save asks it first (the final read's Q2).
    bool commitBatchNow() { return commitBatchTransaction(); }
    /// WHO TELLS THE USER when a batch cannot commit. A whole gesture's rows
    /// are rolled back then — a script run's library writes, gone — and a warn
    /// line in the log has an audience of one. The app installs a listener that
    /// raises it in the scene-issue bar; a hook rather than a direct call
    /// because half the suites in the tree link this file and none of them may
    /// be made to link the UI services (the Database::setDependencyListener
    /// idiom). One process-wide listener, called on EVERY batch commit: `ok`
    /// false = the gesture's rows were rolled back, true = the library is
    /// writable and the condition (if any) is over. The sink is idempotent, so
    /// there is deliberately no state filter here — see announceBatchCommit.
    using BatchCommitListener = std::function<void(bool ok)>;
    static void setBatchCommitListener(BatchCommitListener listener);
    /// How many levels are open (editor.undoState().dbBatchDepth).
    int  batchDepth() const { return batchOpenCount; }
    /// True while the batch actually holds a transaction — false before the scope's
    /// first write and between a stand-down and the next write. Diagnostics only.
    bool batchTransactionLive() const { return batchTx != nullptr; }
    /// The process's durable write commits so far (DbTransaction::commitCount).
    static quint64 durableCommits() { return DbTransaction::commitCount(); }

    // CREATE ===============================================================================
    bool createProjectsTable();
    bool createThumbnailsTable();
    bool createCollectionsTable();
    bool createAssetsTable();
    bool createDependenciesTable();
    bool createAuthorTable();
    bool createFoldersTable();
    bool createFavoritesTable();
    void createAllTables();
    void createIndexes();
    /// The index statements createIndexes ran over an EXISTING library's rows on the opening thread (its DEFECT
    /// case; 0 when the boot's gate prepared them): the boot's result (services/bootreport.h `indexAtOpen`).
    int indexStatementsAtOpen() const { return mIndexStatementsAtOpen; }
    /// THE BOOT'S INDEX GATE (LARGE-LIBRARY-1; main.cpp, before the shell opens the library): how many of the
    /// library's index statements `path`'s library still needs (0: prepared, nothing to build), and their
    /// build on the calling thread through a connection of its own — each statement its own transaction —
    /// false with the failed statement and SQLite's reason. Over an existing library the build reads every
    /// row (the covering indexes carry the columns stored behind each row's thumbnail: 468 MB at 10,000
    /// assets), so the boot runs it on a worker before any window exists, the splash saying so.
    static int indexStatementsToRun(const QString &path);
    static bool prepareIndexes(const QString &path, QString *error);
    void createCasTables();
    /// True when every table this build creates exists here with exactly the
    /// same columns. False = a library from an older build (FORWARD-ONLY-1:
    /// no migrations; main() wipes it through libraryreset).
    bool schemaMatchesFresh();

    // INSERT ===============================================================================
    bool createProject(const QString &guid,
                       const QString &name,
                       const QByteArray &sceneBlob = QByteArray(),
                       const QByteArray &thumbnail = QByteArray());
    bool createFolder(const QString &folderName, const QString &parentFolder, const QString &guid, const QString &projectGuid, bool visible = true);
    /// THE PROJECT'S FOLDER NAMED `folderName`, CREATED IF IT IS NOT THERE —
    /// and its guid either way. The editor's own hidden folders (Systems for
    /// particle emitters, Presets for applied material presets) are found by
    /// NAME, so every writer needs the existing folder's guid rather than the
    /// fresh one it would have minted: minting one and using it without
    /// creating the folder is what filed every emitter after the first under a
    /// guid nothing owned (small-items round B). Empty on failure.
    QString ensureFolder(const QString &folderName, const QString &projectGuid, bool visible = true);
    /// THE ONE DOOR A CATALOG ROW COMES IN BY (ASSETS-HOME-1). `home` is where
    /// the row lives and `origin` how it came to exist (data/assethomekind.h);
    /// the stored `view_filter` and `project_guid` are DERIVED from the home, so
    /// they cannot disagree. Refused — an empty return and `errorOut` set — when
    /// `assethome::refusal(home, origin)` says so: an Assets row that is neither
    /// an import nor an explicit save, a project row with no project, a library
    /// row carrying a project guid.
    QString createAssetEntry(const QString &guid,
                             const QString &assetname,
                             const int &type,
                             const QString &parent,
                             const assethome::Home &home,
                             assethome::Origin origin,
                             const QString &license = QString(),
                             const QString &author = QString(),
                             const QByteArray &thumbnail = QByteArray(),
                             const QByteArray &properties = QByteArray(),
                             const QByteArray &tags = QByteArray(),
                             const QByteArray &asset = QByteArray(),
                             QString *errorOut = nullptr);

    /// A LISTENER FOR DEPENDENCY-EDGE CHANGES. The editor's asset tray reads
    /// USE from these edges (services/assettray.h — a directly-added image
    /// stops folding into its companion material the moment a node uses it),
    /// so a write here has to reach the tray the way a pin change does. Called
    /// with the edge's project guid, or empty when the mutation cannot name
    /// one (a delete by depender/dependee). One process-wide listener,
    /// installed by services/projectmembership.cpp; none in a test that only
    /// compiles this file.
    using DependencyListener = std::function<void(const QString &projectGuid)>;
    static void setDependencyListener(DependencyListener listener);

    bool createDependency(const int &dependerType,
                          const int &dependeeType,
                          const QString &depender,
                          const QString &dependee,
                          const QString &projectGuid = QString());

    bool addFavorite(const QString &guid);
    bool removeFavorite(const QString &guid);

    /// Creates a drawer (ASSET_DRAWERS_SPEC §2). parent -1 = top level; any
    /// other parent must be an existing drawer. Returns the new drawer's id,
    /// or -1 on failure.
    int createCollection(const QString &collectionName, const int parent = -1);

    // DELETE ===============================================================================
    struct PendingAssetScrub;   // below, with applyAssetScrubs
    /// `deferred`: nested in a caller's transaction, the reaped rows' sidecar and session scrubs are handed up
    /// for the caller to apply after ITS commit (as deleteFolderAndDependencies).
    bool deleteProject(const QString &guid, QVector<PendingAssetScrub> *deferred = nullptr);
	bool destroyTable(const QString &table);
	void wipeDatabase();
    /// LIBRARY DELETE KEEPS PROJECT PINS (owner, 2026-09-09).
    ///
    /// An asset a project PINS is not deleted by a library delete: the row is
    /// UNLISTED (listed = 0) and nothing else changes — no asset_files rows,
    /// no project_assets rows, no sidecar, no content. The asset keeps
    /// resolving by guid for every project that pinned it (open, scene read,
    /// thumbnails, export) and simply stops appearing in library listings.
    /// With NO pins the row is deleted exactly as it always was.
    ///
    /// `force` is the hard delete ("Delete everywhere"): pins are dropped and
    /// the rows go regardless. The CONTENT is never unlinked here either way —
    /// reclaiming it is assets.gc's job.
    ///
    /// Reports whether the write actually ran; an unlist that succeeded is
    /// true, like a delete that succeeded.
    bool deleteAsset(const QString &guid, bool force = false);

    /// A delete whose rows are NOT DURABLE YET (code review 2026-09-10).
    /// deleteAsset drops the asset's sidecar and its session registration the
    /// moment its own statements succeed — correct when it owns the
    /// transaction, wrong when it is NESTED inside one: an outer rollback
    /// brings the rows back without the sidecar (rebuildCatalog then loses the
    /// asset for good) and without the registration. So a nested delete
    /// COLLECTS what it would have scrubbed and the outer function applies it
    /// after ITS commit — the only moment the delete is real.
    struct PendingAssetScrub
    {
        QString guid;
        bool    sidecar = false;   ///< this store owned the asset's sidecar
    };
    /// Drop the session registrations and sidecars of deletes that have now
    /// committed. Safe to call with an empty list.
    void applyAssetScrubs(const QVector<PendingAssetScrub> &pending);

    // ---- DEFERRED ASSET DELETES (CLOSE-1) ---------------------------------
    //
    // A delete that nobody is waiting for, off the disk and off the hot path.
    //
    // THE INCIDENT (owner session 2026-09-14): closing the project blocked the
    // UI thread for 33,156 ms. DeleteSceneNodeCommand's DESTRUCTOR finalises
    // the asset row of a delete no undo can reach any more, and
    // QUndoStack::clear() destroys the whole stack in one go — so a session of
    // scripted spheres (64 per run, deleted and undone several times) turned
    // one mouse click into hundreds of deleteAsset() calls, each one its own
    // transaction, each transaction its own fdatasync. The stack was
    //     ~DeleteSceneNodeCommand -> deleteAsset -> deleteAssetRow
    //         -> DbTransaction::commit -> sqlite3PagerCommitPhaseOne -> fdatasync
    // repeated once per command.
    //
    // THE RULE THIS ENCODES: a destructor must never do synchronous disk I/O,
    // and clearing an undo stack must cost O(n) MEMORY, not O(n) syncs. The
    // command now only ENQUEUES its guid here — an append to a QVector, no
    // statement, no transaction, no sync — and one flush applies the whole
    // batch inside ONE transaction, i.e. one commit and one sync for the lot.
    //
    // WHY A QUEUE AND NOT A SCOPE (the begin/endScrub alternative): a command
    // dies at sites nobody can wrap — QUndoStack chopping the redo branch on
    // the next push, the undo limit evicting the oldest command, ~QUndoStack
    // itself. A scope would only cover the call site that opens it; a queue
    // makes "this destructor touches no disk" a property of the DESTRUCTOR.
    //
    // SAFE TO DEFER, by nature: the row describes a scene node that is already
    // gone from the document and from the user's view (the built-in
    // primitives' Object rows), nothing reads it, and the flush points below
    // cover every way the session can end. Worst case after a hard kill, the
    // row outlives its node until the next delete flush — an invisible row,
    // never a wrong answer.
    //
    // FLUSH POINTS (all of them, deliberately few): UndoService::clear() —
    // the mass-death site, i.e. project close and new project — and
    // closeDatabase(), which every exit path runs while the connection is
    // still open (~MainWindow drains the stack itself, step 5 of the shutdown
    // order, and the flush there is explicit as well).

    /// Queue an asset row for deletion. Touches no disk: append only.
    void enqueueAssetDelete(const QString &guid, bool force = false);
    /// How many deletes are waiting (editor.undoState().pendingAssetDeletes).
    int  pendingAssetDeleteCount() const { return pendingAssetDeletes.size(); }
    /// Apply every queued delete in ONE transaction — one commit, one sync.
    /// Returns the number of rows the flush processed. A closed connection or
    /// an already-open outer transaction is not an error: nothing is applied,
    /// the queue is KEPT and the next flush point takes it.
    int  flushPendingAssetDeletes();
    /// Library visibility, written directly (the unlist half of the above and
    /// the re-list an import performs). False on an unknown guid.
    bool setAssetListed(const QString &guid, bool listed);
    /// Is the row a library tile? True for every row a delete never touched;
    /// false for an unlisted row AND for a guid that names nothing.
    bool isAssetListed(const QString &guid);
    /// Deletes a drawer AND its sub-drawers; every asset of the subtree moves
    /// to Uncategorized (0). Ids <= 0 (the root and Uncategorized) are refused.
    bool deleteCollection(const int &collectionId);
    bool deleteFolder(const QString &guid);
    bool deleteDependency(const QString &dependee);
    bool deleteDependency(const QString &depender, const QString &dependee);
    bool removeDependenciesByType(const QString &depender, const ModelTypes &type);
    /// The returned list is the on-disk files the caller should unlink. The
    /// optional `ok` reports whether every row actually went: a delete that
    /// could not run (closed connection, failed statement) must never look
    /// like a successful one to the caller. NESTED in a caller's transaction,
    /// the rows are real only at the caller's commit: with `deferred` the
    /// sidecar and session scrubs are handed up for the caller to apply after
    /// it (applyAssetScrubs), never run here (the final read's B4).
    QStringList deleteFolderAndDependencies(const QString &guid, bool *ok = nullptr,
                                            QVector<PendingAssetScrub> *deferred = nullptr);
    /// `force` carries the hard delete through the whole closure (see
    /// deleteAsset): without it, any member a project still pins is unlisted
    /// instead of removed. `deferred`: as deleteFolderAndDependencies.
    QStringList deleteAssetAndDependencies(const QString &guid, bool *ok = nullptr,
                                           bool force = false,
                                           QVector<PendingAssetScrub> *deferred = nullptr);

    // UPDATE ===============================================================================
    bool renameProject(const QString &guid, const QString &newName);
    bool renameFolder(const QString &guid, const QString &newName);
    bool renameCollection(const int &collectionId, const QString &newName);
    /// Reparents a drawer (parent -1 = top level). Refuses ids <= 0, unknown
    /// drawers/parents, and cycles (a drawer cannot move under its own subtree).
    bool setCollectionParent(const int collectionId, const int parent);
    /// WHERE A ROW IS FILED (DRAWERS-1). `assets.parent` carries both kinds of
    /// parent — a FOLDER guid for a filed row, the owning ASSET's guid for an
    /// import member — so this is only ever called on a row the project owns,
    /// with a folder guid (or the project guid for the root). The membership
    /// rule in services/assettray.h reads the same column.
    bool setAssetParent(const QString &guid, const QString &parent);
    /// Reparents a project FOLDER. Cycles are the caller's to refuse
    /// (services/projectfolders.h does).
    bool setFolderParent(const QString &guid, const QString &parent);
    /// WHERE THIS PROJECT FILES ITS PIN on a library asset (DRAWERS-1). Empty
    /// = the project root. True when the pin row exists and took the value.
    bool setProjectAssetFolder(const QString &projectGuid, const QString &assetGuid,
                               const QString &folder);
    bool updateProject(const QByteArray &sceneBlob, const QByteArray &thumbnail, const QString &projectGuid);
    bool updateProjectBlob(const QByteArray &sceneBlob, const QString &projectGuid);
    /// The two writers above succeed only when they wrote exactly ONE row (`rows`: the statement's changes()).
    static bool wroteOneProjectRow(int rows, const QString &projectGuid);
    bool updateAssetThumbnail(const QString &guid, const QByteArray &thumbnail);
    /// THE ONE IN-PLACE EDIT OF A ROW's BLOB: writes `asset` AND its stamp (blobStamp) into
    /// `assets.hash` in one statement - the stamp is a preview-key input (PREVIEWS-1), so an edit
    /// of the blob moves the key with no read of the blob at a listing.
    bool updateAssetAsset(const QString &guid, const QByteArray &asset);
    /// `assets.hash`: the first 8 bytes of the blob's SHA-256, 16 hex digits; null for no blob.
    /// Written wherever a row's blob is written (the mint, the edit door, an archive's import).
    static QString blobStamp(const QByteArray &asset);
    bool updateSceneThumbnail(const QString &guid, const QByteArray &asset);
    bool updateAssetMetadata(const QString &guid, const QString &name, const QByteArray &tags);
    bool updateAssetProperties(const QString &guid, const QByteArray &asset);
    /// RE-HOME an existing row, IN PLACE (its guid, so everything that points
    /// at it stays valid): the same door rule as createAssetEntry, the
    /// `view_filter` and `project_guid` written together from `home`. "Save to
    /// Library" for an avatar minted in a project (AVATAR_ASSET_SPEC §4 D6-A)
    /// and the user's import of a picture a material had brought in are the
    /// callers. False (and `errorOut`) on a refusal or a database error.
    bool setAssetHome(const QString &guid, const assethome::Home &home, assethome::Origin origin,
                      QString *errorOut = nullptr);
	bool updateProjectDesktop(const QString &guid, int desktop);

	/// WHERE THIS PROJECT'S FOLDER LIVES — the ROOT it was created under, not
	/// the folder itself (SMALL-UI-A, owner review R1d). Empty means the user's
	/// projects root, which is what every project written before the column
	/// existed says and what every project created without the New Scene
	/// dialog's Browse button still says. ONE reader:
	/// ProjectService::projectFolderFor.
	bool setProjectLocation(const QString &guid, const QString &location);
	QString projectLocation(const QString &guid);
	bool updateProjectPosition(const QString &guid, float x, float y);
	// slider mode (DESKTOP_SLIDER_SPEC.md): filmstrip {row, orderIndex}
	bool updateProjectSliderPos(const QString &guid, int row, int index);

    // FETCH ================================================================================
    AssetRecord fetchAsset(const QString &guid);
    QVector<AssetRecord> fetchAssetsForAssetView();
    /// Stored bytes per asset (sum of its linked CAS objects' sizes), for the
    /// Assets page's list view Size column. guid → bytes; assets with no
    /// stored content are absent.
    QMap<QString, qint64> fetchAssetFileSizes();
    /// THE SAME LISTING for any home that is not a project's: Assets (the grid
    /// above), the Materials module's storage, the Avatar module's.
    QVector<AssetRecord> fetchAssetsInHome(assethome::Kind home, int type = -1);
    /// Every row of `projectGuid` filed directly under `parent` (a folder, or
    /// the project guid for the root), optionally one ModelTypes value. No
    /// row is filtered out: the editor tray's rule is services/assettray.h,
    /// which is where "what is a tile" is decided (the dependee filter that
    /// used to ride here hid every asset a scene USED — lane L13).
    QVector<AssetRecord> fetchChildAssets(const QString &parent, const QString &projectGuid, int filter = -1);
    /// THE SAME LISTING FOR SEVERAL PARENTS AT ONCE, in one statement
    /// (LISTALL-1): `parent IN (...)`, the same project scope, the same
    /// ORDER BY name DESC, so a caller that wants the whole project — the
    /// root and every folder — pays one query instead of one per folder.
    /// Rows come back in parent order is NOT promised: bucket them by
    /// `record.parent`, which is what assettray::listAll does.
    QVector<AssetRecord> fetchChildAssetsIn(const QStringList &parents, const QString &projectGuid,
                                            int filter = -1);
    /// THE MEMBERSHIP RULE as a SQL fragment for the assets row aliased `alias`:
    /// its parent is not another ASSET — an import's Mesh and Texture members, which
    /// ride their Object and are never library tiles of their own. The Assets
    /// page grid and the import's re-listing check AND it in (a member row must
    /// never be re-listed into a listing that hides it). It is the SAME
    /// question the editor tray asks in services/assettray.h, so the two
    /// listings cannot disagree. Definition + rationale: database.cpp.
    static QString memberSubquery(const QString &alias);

    /// A PROJECT'S OWN ROW (ASSETS-SCOPE-1) — THE ONE PREDICATE: `view_filter`
    /// Editor, a project guid, and not a MEMBER of somebody else's row (an
    /// import's member rows are Editor too and carry the project the import was
    /// made in, but they belong to their library Object). Such a row exists
    /// only for its project's pins: once none is left it goes — whoever let go
    /// last, and when its project is deleted (deleteProject).
    bool isProjectOwned(const AssetRecord &row);

    // THE ONE TIMESTAMP FORMAT of the projects table's `last_written` and
    // `last_accessed`: SQLite `datetime()`'s own UTC `YYYY-MM-DD HH:MM:SS`,
    // written by `datetime()` in SQL on every write — create, save AND an
    // archive import (which stamps the import moment and reads no stamp from
    // the archive). The Desktop orders by the column as TEXT.

    /// BATCH READS for the listing rules (small-items round B). Each is ONE
    /// query for a whole listing's worth of rows; the per-row reads they
    /// replace made the editor tray cost a query per tile, on every edge write
    /// and every search keystroke.
    ///
    /// Which of `guids` name a row that EXISTS, and which project owns it:
    /// guid → project_guid (empty for a library row). A guid that names no row
    /// is absent from the hash — that is the "is my parent an asset?" test.
    QHash<QString, QString> fetchAssetOwners(const QStringList &guids);
    /// The stored `asset` blob of each of `guids` that has one, guid → bytes.
    QHash<QString, QByteArray> fetchAssetDataFor(const QStringList &guids);
    /// For each of `dependees` that something uses (unscoped, as hasMultipleDependers): true when EVERY
    /// depender is a material row, false when one is not (another type, or a depender the catalog does
    /// not know: a scene node's use); a dependee nothing uses is absent. The batch form of the V-2 fold's
    /// per-row questions (LARGE-LIBRARY-1), answered from idx_assets_material_copies.
    QHash<QString, bool> fetchUsedOnlyByMaterials(const QStringList &dependees);
    /// The dependency edges recorded for `projectGuid` whose dependee is one of
    /// `dependees`, as dependee → dependers (duplicates removed). The batch form
    /// of fetchDependers.
    QHash<QString, QStringList> fetchProjectDependers(const QString &projectGuid, const QStringList &dependees);
    /// The guids this project pins (project_assets rows), without reading a
    /// catalog row for each. The batch form of isAssetPinnedBy.
    QSet<QString> fetchProjectPinnedGuids(const QString &projectGuid);
    /// The guids of every FOLDER this project owns, in one query — "is this
    /// row filed somewhere?", which is how a listing tells a row that belongs
    /// in a folder from one that belongs at the root.
    QSet<QString> fetchProjectFolderGuids(const QString &projectGuid);
    /// Reference-with-pin membership (ASSET_PIPELINE_SPEC §3.1.5): the LIBRARY
    /// assets this project pinned (project_assets rows), as full catalog
    /// records. This is THE source assets.list({scope:'project'}) and the
    /// editor's asset tray share.
    QVector<AssetRecord> fetchProjectPinnedAssets(const QString &projectGuid);
    /// WHERE EACH PIN IS FILED: asset guid -> folder guid, for the pins this
    /// project has filed somewhere (an unfiled pin is absent). ONE query for a
    /// whole listing, like the batch reads above — the tray reads it on every
    /// edge write and every search keystroke.
    QHash<QString, QString> fetchProjectPinFolders(const QString &projectGuid);
    /// The DEPENDERS of `dependee` among the dependency edges recorded for
    /// `projectGuid` — who in this project uses the asset (a node's material
    /// slot, the ground, a decal, an emitter, a material). Duplicates removed.
    QStringList fetchDependers(const QString &dependee, const QString &projectGuid);
    /// Every dependency edge `depender` has, recorded for `projectGuid` — what
    /// a scene node USES, as the catalog records it (material.reset's
    /// ResetMaterialCommand drops and restores a subset of these).
    QVector<DependencyRecord> fetchNodeDependencies(const QString &depender,
                                                    const QString &projectGuid);
    /// The other direction: which PROJECTS pin this asset (guid + name, named
    /// by the projects table; a pin whose project row is gone reports the guid
    /// as its name). This is what `assets.pins` answers and what the Assets
    /// page's delete confirmation counts.
    QVector<AssetPinRecord> fetchAssetPins(const QString &guid);
    /// How many projects pin this asset — the count without the names. `readOk` is CLEARED when the count could
    /// not be read (0 is then no answer: the delete paths treat it as pinned); left as it is otherwise.
    int countAssetPins(const QString &guid, bool *readOk = nullptr);
    /// Does THIS project pin THIS asset? The row's existence, not its content:
    /// a DB-only asset (a material with no stored bytes) is pinned with an
    /// EMPTY oid, so AssetCas::pinnedOid cannot answer this question.
    bool isAssetPinnedBy(const QString &projectGuid, const QString &assetGuid);
    /// Drop ONE project's pin on an asset (project_assets row). True when the row
    /// went or was never there; false on a database error.
    bool unpinAsset(const QString &projectGuid, const QString &assetGuid);
    QVector<AssetRecord> fetchAssetsFromParent(const QString &guid);
	QVector<AssetRecord> fetchAssetsByType(const int &type, const QString &projectGuid);
    QVector<AssetRecord> fetchFilteredAssets(const QString &guid, const int &type);
    QVector<AssetRecord> fetchFavorites();
    QVector<CollectionRecord> fetchCollections();
    /// The drawer plus all its descendants (breadth-first); empty when the id
    /// names no drawer row.
    QVector<int> fetchCollectionSubtree(const int collectionId);
    /// How many asset rows live in these drawers (the delete confirm).
    int countAssetsInCollections(const QVector<int> &collectionIds);
    /// THE PROJECTS AS A LISTING — no thumbnail, never (D11-LIBRARY-SCALE: the
    /// tile cache reads a tile's picture by guid). desktop <= 0 lists every
    /// project; 1..N the projects on that desktop.
    QVector<ProjectTileData> fetchProjects(int desktop = 0);
    /// ONE project's desktop row, no thumbnail (the grid's incremental add and
    /// the guid resolve — O(1) by the primary key); false when no row has `guid`.
    bool fetchProjectTile(const QString &guid, ProjectTileData *out);
    /// Whether a project row has `guid`, told apart from a library that could not be read: `readOk` false =
    /// nothing is known (the folder sweep then touches nothing).
    bool hasProjectRow(const QString &guid, bool *readOk);
    /// The projects called exactly `name` (the name half of a guid-or-name resolve).
    QVector<ProjectTileData> fetchProjectsNamed(const QString &name);
    /// THE PROJECT NAME RULE (PROJECT-NAMES-1), the one place it lives. A
    /// project is IDENTIFIED by its guid; its name is a label, and two labels
    /// never match (case-insensitively). Returns `wanted` trimmed when no
    /// project but `exceptGuid` carries it, else the first free `<stem> N`
    /// (N = 2, 3, ...) — "Matcaps", "Matcaps 2", "Matcaps 3". A trailing " N"
    /// is read as a copy number ONLY when `wanted` is a copy this rule made: its
    /// stem and every "<stem> k" for 2 <= k < N taken (an unbroken chain). So a
    /// second "Matcaps 2" becomes "Matcaps 3", never "Matcaps 2 2", while
    /// "Area 51" stays the user's name and becomes "Area 51 2". Empty
    /// in, empty out. Every door that names a project uses it: the create, the
    /// rename and the archive import (a reopened sample is a fresh copy).
    QString nextFreeProjectName(const QString &wanted, const QString &exceptGuid = QString());
    /// The guids on one desktop, no blobs — what the Desktop entry checks its
    /// tiles against without reading a thumbnail.
    QStringList fetchProjectGuids(int desktop);
    QVector<FolderRecord> fetchChildFolders(const QString &parent, const QString &projectGuid);
    /// ONE folder row by guid; an empty `guid` field when nothing has it.
    FolderRecord fetchFolder(const QString &guid);
    QVector<FolderRecord> fetchCrumbTrail(const QString &parent, const QString &projectGuid);
    /// THE TILE CACHE'S BY-GUID READS (ui/controls/tilecache.h): the thumbnail
    /// bytes of at most one batch of guids, one bound IN (...) each. The only
    /// queries that select a thumbnail for more than one row.
    QHash<QString, QByteArray> fetchProjectThumbnailBytes(const QStringList &guids);   ///< one project's, on this thread
    /// THE TILE CACHE'S READ, OFF THE UI THREAD (TC2-LIBRARY-CREATE-READ, 2026-10-10): the same one
    /// bound `SELECT guid, thumbnail ... WHERE guid IN (...)` on a READ-ONLY connection of its own to
    /// `dbPath`, opened and closed on the CALLING thread — the tile cache's pool calls it, so the
    /// bytes of a Desktop of 500 projects (~190 KB a picture) are never read on the UI thread.
    /// Logged in the query log under the old names (fetchAssetThumbnailBytes / fetchProjectThumbnailBytes).
    enum class ThumbnailTable { Assets, Projects };
    static QHash<QString, QByteArray> readThumbnailBytes(const QString &dbPath, ThumbnailTable table,
                                                         const QStringList &guids);
    /// THE PREVIEW WRITER's library half (PREVIEWS-1; the thread and the queue are
    /// services/previewwriter.h). openPreviewConnection opens `connectionName` to `dbPath` on the
    /// CALLING thread with SQLite's busy timeout at `busyTimeoutMs` and the library's durability
    /// pragmas; the caller closes it and removes the name. writePreview stores one preview —
    /// `assets.thumbnail` and the `thumbnails` row {guid, hash = key, last_written} — in ONE
    /// transaction (BEGIN IMMEDIATE .. COMMIT), or nothing: an empty PNG, a missing row or any
    /// statement's failure rolls back; `busy` says the lock was not had within the timeout.
    static bool openPreviewConnection(QSqlDatabase *conn, const QString &dbPath,
                                      const QString &connectionName, int busyTimeoutMs,
                                      QString *errorOut);
    struct PreviewWrite
    {
        bool ok = false;
        bool busy = false;
        QString reason;   ///< empty when ok
    };
    static PreviewWrite writePreview(QSqlDatabase conn, const QString &guid, const QByteArray &png,
                                     const QString &key);
    /// The thumbnail hook (setAssetThumbnailWritten) for a write made off this connection; called
    /// on the UI thread by whoever made it.
    static void announceAssetThumbnailWritten(const QString &guid);
    /// AN IMPORT's PICTURES (PREVIEWS-1 stage 2): an import's commit writes NO tile; after its
    /// transaction commits, each staged row's picture goes to this sink — the preview service's
    /// store door, which reads the row's key THEN and hands bytes + key to the one preview writer
    /// (services/previewservice.h). Set on THIS library's instance by the preview service's owner
    /// (ShellServices) and cleared before that service goes — never a process-wide hook. Unset (a
    /// suite that links the import alone, a host with no preview service) an import stores no
    /// picture: its rows read `missing` until a render draws them. A NOTIFICATION DOOR ONLY: the
    /// Database hands the picture on and nothing more - it never renders, never decides a key and
    /// never waits (the preview service does all three); it is the one object all nine import
    /// construction sites share, which is why the sink lives here.
    using ImportPictureSink = std::function<void(const QString &guid, const QImage &picture)>;
    void setImportPictureSink(ImportPictureSink sink) { importPictureSink = std::move(sink); }
    const ImportPictureSink &importPictures() const { return importPictureSink; }
    /// WHAT A PREVIEW DEPICTS, AND WHAT IS STORED (PREVIEWS-1; services/previewkey.h): per row its
    /// type, whether it holds bytes and a definition blob, its stored key, and the key's inputs —
    /// its asset_files role:oid, `projectGuid`'s pin of it, its members' dependee:oid. ONE
    /// statement a call however many rows (LARGE-LIBRARY-1's law); no thumbnail or blob is read
    /// (their lengths only). `projectOnly` keeps `projectGuid`'s pinned rows, `type` >= 0 one
    /// type, a non-empty `guids` those rows.
    struct PreviewRow
    {
        QString guid;
        int type = 0;
        bool hasBytes = false;
        bool hasBlob = false;
        QString storedKey;
        QString blobStamp;   ///< `assets.hash` (Database::blobStamp): moves with an in-place blob edit
        QStringList files;
        QString pin;
        QStringList members;
        /// PLATFORM FURNITURE (ATOM P2): the app's own content - the default floor's checker, the
        /// shipped meshes (services/shippedassets.h) - shown in no tile grid: nothing to draw.
        bool platform = false;
    };
    QVector<PreviewRow> fetchPreviewRows(const QString &projectGuid, bool projectOnly, int type,
                                         const QStringList &guids = QStringList());
    /// The library file this connection opened (what readThumbnailBytes is handed).
    QString filePath() const;
    QByteArray fetchAssetData(const QString &guid) const;

    QStringList fetchFolderNameByParent(const QString &guid);
    QStringList fetchAssetNameByParent(const QString &guid);
    /// The closure reads of the two deletes below: `readOk` (optional) is CLEARED when a read failed - a failed
    /// read is never a shorter closure (NOQT-DATABASE-1 stage 2) - and left as it is otherwise.
    QStringList fetchFolderAndChildFolders(const QString &guid, bool *readOk = nullptr);
    QStringList fetchChildFolderAssets(const QString &guid, bool *readOk = nullptr);
    QStringList fetchAssetGUIDAndDependencies(const QString &guid, bool appendSelf = true, bool *readOk = nullptr);
    QStringList fetchAssetAndAllDependencies(const QString &guid);
    /// THE CLOSURE'S BATCHED READS (D11-LIBRARY-SCALE): the catalog edges of
    /// many dependers in one statement per chunk (depender -> its dependees, in
    /// its own order); the closure's rows WITHOUT a BLOB; and one row's two JSON
    /// BLOBs, read when that asset is written out.
    /// `withMemberObjects`: each depender that is a MESH MEMBER also answers its OBJECT row (its
    /// `parent`, when that row is an Object) after its dependencies — the member-to-object edge a
    /// closure needs (the member's import record lives on the Object), in the SAME statement.
    QHash<QString, QStringList> fetchDependencyEdges(const QStringList &dependers,
                                                     bool withMemberObjects = false);
    QVector<AssetRecord> fetchAssetHeaders(const QStringList &guids);
    bool fetchAssetRowBlobs(const QString &guid, QByteArray *asset, QByteArray *properties);
    QVector<DependencyRecord> fetchAssetDependencies(const AssetRecord &record);
    QStringList fetchAssetDependeesByType(const QString &guid, const ModelTypes&);
    QStringList fetchAssetAndDependencies(const QString &guid, bool *readOk = nullptr);
    /// A member on this library's connection (NOQT-DATABASE-1 stage 2: it was static on the Qt default
    /// connection). Its callers hold the library: `AssetModelPanel::addFavorites` reaches it only for a
    /// favourite its `handle` listed (populateFavorites returns nothing with no handle).
    QString fetchObjectMesh(const QString &guid, const int ertype, const int eetype);

    QStringList hasMultipleDependers(const QString &guid);
    bool hasDependencies(const QString &guid);

    // IMPORT ===============================================================================
    /// The PROJECT archive (.jaf) is the last .jaf reader/writer; JAF-READER-1 replaces it with manifest v2 + the envelope.
    /// ARCHIVE-GUIDS-1: `assetGuids` receives ONLY the collision remaps (a
    /// row keeps its guid otherwise; `value(guid, guid)` reads identity), and
    /// `knownGuids` the archive guids this library already held at the same
    /// type — the same asset, whose row the import left alone.
    /// `whyOut`, when the import is refused for a reason the user can act on (an archive scene that is
    /// not a JSON object, STUDIO-HYGIENE-1), says so; it stays empty for a database failure.
    bool importProject(const QString &inFilePath, const QString &newGuid, QString &worldName, QMap<QString, QString> &assetGuids, QSet<QString> *knownGuids = nullptr, QString *whyOut = nullptr);

    // EXPORT ===============================================================================
    /// THE PROJECT ARCHIVE'S CATALOG SNAPSHOT, in two halves (STUDIO-D1 item 4): the READ
    /// is this library connection's (the UI thread's), the WRITE is a new SQLite file —
    /// its journal and its fsync — on whatever thread calls it (the archive's worker).
    struct ExportCatalog
    {
        bool found = false;             ///< the project row exists
        /// A read that FAILED, by its statement's name (said in the log by it): the archive is then not written,
        /// never written short of rows. Empty when every read ran.
        QString failure;
        QString name, version, lastWritten, lastAccessed, guid;
        QByteArray scene, thumbnail;
        QVector<AssetRecord> assets;    ///< the project's rows + its pinned library rows
        QVector<DependencyRecord> dependencies;
        QVector<FolderRecord> folders;
    };
    ExportCatalog readExportCatalog(const QString &projectGuid);
    /// Writes `<outDir>/<guid>.db` through its own side connection. False with
    /// `errorOut` set when any statement failed.
    static bool writeExportCatalog(const QString &outDir, const ExportCatalog &catalog,
                                   QString *errorOut);

    bool checkIfTableExists(const QString &tableName);
    bool checkIfColumnExists(const QString &tableName, const QString &columnName);


	/// Both return false when the UPDATE did not run (they used to be void and
	/// had never worked — positional SQL bound by name, see the .cpp).
	bool updateGlobalDependencyDepender(const int &type, const QString &depender, const QString &dependee);
	bool updateGlobalDependencyDependee(const int &type, const QString &depender, const QString &dependee);

	QString getDependencyByType(const int &type, const QString &depender);

    // MISC
	void updateAuthorInfo(const QString &author_name);
	bool isAuthorInfoPresent();
	QString getAuthorName();
    bool switchAssetCollection(const int, const QString&);

	bool checkIfDependencyExists(const QString& depender, const QString& dependee);
    bool checkIfProjectVersionSupported(const QString& pathToDb);

    /// Is a project archive's recorded CONTENT_VERSION string ("0.9.1b",
    /// "1.0.0", …) new enough to open? Encodes MIN_JAF_VERSION's historic
    /// major*10 + minor packing as an ordered (major, minor) compare, parsed
    /// with integers — the old code ran the first three characters through
    /// toFloat() and multiplied by 10, which happened to land on the right
    /// integer only because 0.9f*10 rounds up to exactly 9.0f, and rejected
    /// every 0.10.x archive outright ("0.1" → 1). Static + public so the gate
    /// is unit-testable without a database file (deep audit 2026-09, area 6).
    static bool jafVersionAccepted(const QString &version);

    /// The library's connection on the std-only layer (src/data/db/, NOQT-DATABASE-1): the handle
    /// under Qt's default connection, BORROWED until the Database opens its own (ruling 3). No
    /// handle before initializeDatabase or after closeDatabase: a statement on it answers a named error.
    /// Its name is kEditorConnection (MeshBakeStore's shared settings cache is filled by its lookups only).
    db::Connection &connection() { return *mConnection; }
    static constexpr const char *kEditorConnection = "editor";

private:
    bool checkIfVersionSupported(const QString& pathToDb, const QString& table_name);

    /// deleteAsset's body. `deferred` non-null + a NESTED transaction = the
    /// caller owns the scrub (see PendingAssetScrub); otherwise this scrubs
    /// inline, exactly as before.
    bool deleteAssetRow(const QString &guid, bool force,
                        QVector<PendingAssetScrub> *deferred);

    /// SIDECAR LIFECYCLE (invariant I2 — deep audit 2026-09, area 6).
    ///
    /// <store>/sidecar/<guid>.json is the record rebuildCatalog reconstructs
    /// the library from, so it has to track the catalog, not just its birth:
    /// sidecars used to be written at IMPORT ONLY, which left 41 of the
    /// owner's 86 naming deleted assets and every rename/retag/refile invisible
    /// to recovery. Every catalog mutation that changes a field the sidecar
    /// records now refreshes it, and deleteAsset removes it.
    ///
    /// Both are best-effort and deliberately quiet: an offline store or an
    /// asset with no sidecar must never turn a successful database write into
    /// a failure (the row is the truth; the sidecar is its backup).
    /// Does the ACTIVE store root belong with THIS catalog for this guid?
    /// (A sidecar already there, or objects this catalog records present
    /// under it.) Guards both calls below — see the definition for why a
    /// guid-only key would have every unit test write into the developer's
    /// live library.
    bool sidecarBelongsHere(const QString &guid);
    void refreshSidecar(const QString &guid);
    void dropSidecar(const QString &guid);

    QString projectsTableSchema;
    QString thumbnailsTableSchema;
    QString collectionsTableSchema;
    QString assetsTableSchema;
    QString dependenciesTableSchema;
    QString authorTableSchema;
    QString foldersTableSchema;
    QString favoritesTableSchema;


    QSqlDatabase db;
    int mIndexStatementsAtOpen = 0;   ///< indexStatementsAtOpen
    std::unique_ptr<db::Connection> mConnection;   ///< connection(); borrowed from `db`
    ImportPictureSink importPictureSink;   ///< setImportPictureSink

    /// The gesture batch (see beginBatch). `batchOpenCount` is the counted
    /// scope; `batchTx` is the transaction behind it while it is open (a
    /// db::Transaction on connection(), the lock's holder named "the editor's
    /// gesture batch") — gone when the batch stands down for a nested owner,
    /// back at the next write.
    int  batchOpenCount = 0;
    std::unique_ptr<db::Transaction> batchTx;
    /// Levels the database DROPPED while their guards were still alive
    /// (closeDatabase). The next endBatch calls absorb them silently: the
    /// guard is unwinding through a close it never asked for, and warning it
    /// about an imbalance it did not cause is noise.
    int  batchScopesAbandoned = 0;
    /// Opens the batch transaction (IMMEDIATE) if the scope is open and it is not live (and nothing
    /// else owns a transaction on this connection). Called at a WRITE only (beginBatchAtWrite).
    /// False only when it tried and the BEGIN failed.
    bool resumeBatchIfNeeded();
    /// The write funnel's half: the instance whose connection runs `query` opens its batch; false
    /// when that BEGIN failed (the write is then refused). A static walk of the registry:
    /// executeAndCheckQuery never touches `this`.
    static bool beginBatchAtWrite(const DbQuery &query);
    /// Commits the batch transaction if it is live, leaving the SCOPE open.
    /// The stand-down path and the outermost endBatch both go through it.
    bool commitBatchTransaction();
    /// Tells the listener that the library's writability CHANGED (H7).
    void announceBatchCommit(bool ok);
    /// The stand-down hook's per-instance half: commit and go quiet if the
    /// connection about to open a transaction is OURS.
    void standDownBatchFor(const db::Connection &connection);

    /// The deferred-delete queue (see enqueueAssetDelete). Guid + the hard
    /// delete flag, in the order the commands died.
    struct PendingAssetDelete
    {
        QString guid;
        bool    force = false;
    };
    QVector<PendingAssetDelete> pendingAssetDeletes;
};

/// RAII over Database::beginBatch/endBatch — the way callers should open one.
/// Null-safe (a host with no database is common in this codebase), copy-free.
class DbBatch
{
public:
    explicit DbBatch(Database *database) : db(database) { if (db) db->beginBatch(); }
    ~DbBatch() { end(); }

    /// Ends the scope NOW and reports whether the commit it owned succeeded
    /// (true for an inner level, and for no database at all). Idempotent; the
    /// destructor calls it. A caller that must know whether the gesture's rows
    /// survived — rather than leaving it to the listener — uses this.
    bool end()
    {
        if (!db) return true;
        Database *owner = db;
        db = nullptr;
        return owner->endBatch();
    }

    DbBatch(const DbBatch &) = delete;
    DbBatch &operator=(const DbBatch &) = delete;

private:
    Database *db = nullptr;
};

#endif // DATABASE_H
