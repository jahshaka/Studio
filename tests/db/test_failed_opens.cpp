/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// db.failed_opens (NOQT-DATABASE-1 stage 2, the audit's findings 6 and 7) — A FAILED READ AT AN OPEN
// REFUSES THE OPEN BY NAME; it never decides a reset, and never opens an empty world:
//   1. the startup generation check (librarygeneration::checkAndWipe) on a library whose `PRAGMA
//      user_version` cannot be read (a file that is not a database: SQLITE_NOTADB at the first read)
//      is REFUSED with the read named, the file's bytes untouched and the reset never reached — beside
//      an OLDER library that does reach the reset (the control: the recorder counts) and a CURRENT one
//      that does not;
//   2. a project open's scene read (openreads::readPlan) whose step fails (a view raising "integer
//      overflow" on the row) or whose read connection never opened (a path that cannot be opened)
//      is REFUSED by name — never `found` false with no refusal, which opens an empty world the next
//      save writes over the scene — beside a scene that reads (found) and a guid with no row ("no such
//      scene": found false, no refusal, as before);
//   3. the library reset's step after its rebuild (the audit's finding 1): the REAL libraryreset::keepAfterRebuild
//      for every rebuild answer - the kept store is collected only after a rebuild that is ok, failed no row and
//      committed, and otherwise the reset fails with the rebuild's reason (what it DID, the sweep recorded);
//   4. THE RESET's REAL ORDER (the second confirm's finding 1): a reset whose rebuild COMMITS and whose LATER step
//      fails (the bootstrap's primitive seed) leaves the library NOT this build's (user_version 0, read back from the
//      file), so the next checkAndWipe reaches the reset again; only a whole reset is stamped (the control);
//   5. A REFUSED CLEAR DATABASE (a current library, a kept storage, a rebuild refused for a row it could not write)
//      seeds NOTHING after the refusal and is left NOT this build's: the kept sidecar and its object survive the
//      reset's own forced sweep (it never ran) and an ordinary sweep after it (the empty catalog's refusal holds);
//      beside a store with no sidecar folder at all, whose reset is whole (a retry after a crash finishes);
//   6. the reset's mark (generation 0) is COMMITTED before step 2 even inside a script run's open gesture batch;
//   7. a failed reset over a pending library answers `pending` - the session ends - with the one sentence, on every
//      failing step.
//
// THE LINK, test-side only: libraryreset::reset is a RECORDER that runs the reset's REAL catalog steps
// (libraryreset::finishCatalog, libraryresetkeep.cpp: the wipe, the real rebuild, keepAfterRebuild, the real sweep,
// the stamp) on the throwaway library - only its file steps (1-3) and its refusal prelude are the product's
// libraryreset.cpp, not linked here (the seeder's and the importer's machinery); libraryreset::bootstrap (step 5's
// body) is a recorder that does what the primitives seed does to the catalog (one platform row) and can answer an
// error; SceneReader's two source collectors answer empty lists (readPlan calls them only for a scene that was found).
// A row the rebuild cannot write is a TEMP TRIGGER the test puts on the rebuild's OWN connection as it opens
// (sqlite3_auto_extension, registered only around that call).

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlDriver>

#include <cstdio>
#include <cstring>
#include <string>

#include <sqlite3.h>

#include "data/assethomekind.h"
#include "data/database/database.h"
#include "data/db/connection.h"
#include "data/db/statement.h"
#include "io/scenereader.h"
#include "services/assetgc.h"
#include "services/assetmigration.h"
#include "services/assetstorepaths.h"
#include "services/librarygeneration.h"
#include "services/libraryreset.h"
#include "services/openreads.h"

namespace {

int failures = 0;
int gResets = 0;
int gBootstraps = 0;            // step 5's runs (the recorder below)
bool gBootstrapError = false;   // the recorder answers a primitive seed error (case 4)

void check(bool ok, const QString &what)
{
    std::printf("%s %s\n", ok ? "ok:  " : "FAIL:", qUtf8Printable(what));
    std::fflush(stdout);
    if (!ok) ++failures;
}

void removeLibrary(const QString &path)
{
    for (const char *suffix : { "", "-wal", "-shm", "-journal", ".lock" }) QFile::remove(path + QLatin1String(suffix));
}

/// A library of this build's schema at `path` (the real Database's createAllTables), closed.
void makeLibrary(const QString &path)
{
    removeLibrary(path);
    Database library;
    library.initializeDatabase(path);
    library.createAllTables();
    library.closeDatabase();
}

void exec(const QString &path, const std::string &sql)
{
    db::Options o;
    o.name = "failed-opens-setup";
    db::Error e;
    db::Connection c = db::Connection::open(path.toStdString(), o, &e);
    c.exec(sql, &e);
    check(!e, QStringLiteral("setup: %1 %2").arg(QString::fromStdString(sql), QString::fromStdString(e.message)));
}

/// The library's generation as the next boot reads it (-1: unread).
int generationOf(const QString &path)
{
    db::Options o;
    o.name = "failed-opens-generation";
    db::Error e;
    db::Connection c = db::Connection::open(path.toStdString(), o, &e);
    db::Statement s(c, "PRAGMA user_version");
    return s.step(&e) ? int(s.int64(0)) : -1;
}

/// A fresh store at `root` (its sidecar folder made when `withSidecars`), made the process's store.
void freshStore(const QString &root, bool withSidecars)
{
    QDir(root).removeRecursively();
    QDir().mkpath(withSidecars ? QDir(root).filePath(QStringLiteral("sidecar")) : root);
    AssetStorePaths::setRootOverride(root);
}

/// A KEPT asset in the store: a Materials-library sidecar the rebuild places, and its one object. Answers the object.
QString keptAsset(const QString &root, const QString &guid, const QString &oid)
{
    const QString object = AssetStorePaths::objectPathIn(root, oid, QStringLiteral("png"));
    QDir().mkpath(QFileInfo(object).absolutePath());
    {
        QFile f(object);
        check(f.open(QIODevice::WriteOnly) && f.write("kept bytes") > 0, QStringLiteral("setup: the object of %1").arg(guid));
    }
    QJsonObject file;
    file[QStringLiteral("oid")] = oid;
    file[QStringLiteral("ext")] = QStringLiteral("png");
    file[QStringLiteral("role")] = QStringLiteral("source");
    file[QStringLiteral("name")] = QStringLiteral("kept.png");
    QJsonObject sidecar;
    sidecar[QStringLiteral("formatVersion")] = 3;
    sidecar[QStringLiteral("guid")] = guid;
    sidecar[QStringLiteral("name")] = guid;
    sidecar[QStringLiteral("type")] = 1;
    sidecar[QStringLiteral("home")] = assethome::kindName(assethome::Kind::MaterialsLibrary);
    sidecar[QStringLiteral("origin")] = QStringLiteral("create");
    sidecar[QStringLiteral("files")] = QJsonArray{ file };
    QFile f(AssetStorePaths::sidecarPathIn(root, guid));
    check(f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(sidecar).toJson()) > 0,
          QStringLiteral("setup: the sidecar of %1").arg(guid));
    return object;
}

/// THE INJECTION: a row the rebuild cannot write. The rebuild opens its OWN connection (AssetMigration's openOwn), so
/// the trigger is put on every connection opened while this is registered (sqlite3_auto_extension), as it opens.
int poisonOnOpen(sqlite3 *handle, char **, const sqlite3_api_routines *)
{
    sqlite3_exec(handle, "CREATE TEMP TRIGGER poison BEFORE INSERT ON main.assets WHEN NEW.guid = 'cd-poisoned' "
                         "BEGIN SELECT RAISE(ABORT, 'injected: this row is refused'); END", nullptr, nullptr, nullptr);
    return SQLITE_OK;
}

/// The library's raw handle (the editor's connection is still Qt's default one until stage 3): the test's authorizer.
sqlite3 *libraryHandle()
{
    return *static_cast<sqlite3 *const *>(QSqlDatabase::database().driver()->handle().constData());
}

/// THE TEST's REFUSALS on the library's own handle (case 7): 1 = every DROP TABLE (the wipe); 2 = a WRITE of
/// `user_version` other than 0 (the stamp - the reset's own mark, 0, still lands).
int gRefuse = 0;
int refuseOnLibrary(void *, int action, const char *arg1, const char *arg2, const char *, const char *)
{
    if (gRefuse == 1 && action == SQLITE_DROP_TABLE) return SQLITE_DENY;
    if (gRefuse == 2 && action == SQLITE_PRAGMA && arg1 && std::strcmp(arg1, "user_version") == 0 && arg2
        && std::strcmp(arg2, "0") != 0)
        return SQLITE_DENY;
    return SQLITE_OK;
}

/// The reset's catalog steps, run on `db` over `storeRoot` - THE PRODUCT's, in its order (markPending, then
/// libraryreset::finishCatalog).
void catalogSteps(Database *db, const QString &storeRoot, const libraryreset::Keep &keep, libraryreset::Result &result)
{
    if (libraryreset::markPending(db, result))   // the reset's first write (its file steps 2-3 follow it there)
        libraryreset::finishCatalog(db, nullptr, storeRoot, keep, /*seedPresets*/ false, result);
}

}   // namespace

// ---- the recorders (above) ------------------------------------------------------------------------
namespace libraryreset {
QString refusalReason(const PreviewService *) { return QString(); }
Result reset(Database *db, SettingsManager *, const QString &, const std::function<QString(const QString &)> &, bool,
             const Keep &keep)
{
    ++gResets;
    Result result;
    catalogSteps(db, AssetStorePaths::root(), keep, result);
    return result;
}
/// Step 5's body: what the primitives seed does to the catalog - one platform row - and an error when asked.
void bootstrap(Database *db, SettingsManager *, const QString &, bool, Result &result)
{
    ++gBootstraps;
    db::Error e;
    db->connection().exec("INSERT INTO assets (guid, name, type, view_filter) VALUES ('primitive-cube', 'Cube', 5, 0)", &e);
    if (gBootstrapError && result.error.isEmpty()) result.error = QStringLiteral("primitive seed: injected");
}
}   // namespace libraryreset

QStringList SceneReader::collectMeshSources(const QJsonObject &) { return {}; }
QStringList SceneReader::collectClipSources(const QJsonObject &) { return {}; }

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    const QString here = QDir::currentPath();
    // THE STORE IS THE TEST's OWN, before anything can reach the reset (it removes and collects a store).
    freshStore(QDir(here).filePath(QStringLiteral("failed_opens_store")), /*withSidecars*/ true);

    // =======================================================================
    // 1. THE GENERATION CHECK (finding 6).
    // =======================================================================
    {
        const QString older = QDir(here).filePath(QStringLiteral("failed_opens_older.db"));
        makeLibrary(older);
        exec(older, "PRAGMA user_version = " + std::to_string(librarygeneration::generation() - 1));
        gResets = 0;
        const librarygeneration::Result wiped = librarygeneration::checkAndWipe(older, here, nullptr);
        check(gResets == 1 && wiped.outcome == librarygeneration::Outcome::Wiped,
              QStringLiteral("1: CONTROL: an OLDER library reaches the reset (resets %1, outcome %2)")
                  .arg(gResets).arg(int(wiped.outcome)));
        removeLibrary(older);

        const QString current = QDir(here).filePath(QStringLiteral("failed_opens_current.db"));
        makeLibrary(current);
        gResets = 0;
        const librarygeneration::Result kept = librarygeneration::checkAndWipe(current, here, nullptr);
        check(gResets == 0 && kept.outcome == librarygeneration::Outcome::Current,
              QStringLiteral("1: CONTROL: a CURRENT library is left alone (resets %1, outcome %2)")
                  .arg(gResets).arg(int(kept.outcome)));
        removeLibrary(current);

        const QString notADatabase = QDir(here).filePath(QStringLiteral("failed_opens_notadb.db"));
        removeLibrary(notADatabase);
        const QByteArray bytes = QByteArray("this file is not an SQLite library ").repeated(64);
        {
            QFile f(notADatabase);
            check(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), QStringLiteral("1: the unreadable file is written"));
        }
        gResets = 0;
        const librarygeneration::Result refused = librarygeneration::checkAndWipe(notADatabase, here, nullptr);
        check(gResets == 0, QStringLiteral("1: a library whose generation could not be read NEVER reaches the reset (resets %1)")
                                .arg(gResets));
        check(refused.outcome == librarygeneration::Outcome::Refused
                  && refused.reason.contains(QStringLiteral("could not be read")),
              QStringLiteral("1: ...it is REFUSED by name (outcome %1: '%2')").arg(int(refused.outcome)).arg(refused.reason));
        QFile after(notADatabase);
        check(after.open(QIODevice::ReadOnly) && after.readAll() == bytes,
              QStringLiteral("1: ...and its file is untouched"));
        after.close();
        removeLibrary(notADatabase);
    }

    // =======================================================================
    // 2. THE SCENE READ OF AN OPEN (finding 7).
    // =======================================================================
    {
        const QString library = QDir(here).filePath(QStringLiteral("failed_opens_scene.db"));
        makeLibrary(library);
        {
            Database writer;
            writer.initializeDatabase(library);
            check(writer.createProject(QStringLiteral("p-ok"), QStringLiteral("p-ok"), QByteArray("{\"formatVersion\": 2}")),
                  QStringLiteral("2: the scene's project row is written"));
            writer.closeDatabase();
        }

        const auto plan = [](const QString &path, const QString &guid) {
            openreads::Reads reads = openreads::prepare(nullptr, path);
            reads.projectGuid = guid;
            openreads::ReadConnection conn(path);
            openreads::readPlan(conn.db(), nullptr, reads);
            return reads;
        };
        const openreads::Reads ok = plan(library, QStringLiteral("p-ok"));
        check(ok.found && ok.refusal.isEmpty(),
              QStringLiteral("2: CONTROL: a scene that reads is found (found %1, refusal '%2')").arg(ok.found).arg(ok.refusal));
        const openreads::Reads missing = plan(library, QStringLiteral("p-missing"));
        check(!missing.found && missing.refusal.isEmpty(),
              QStringLiteral("2: CONTROL: no such scene stays 'not found', unrefused (found %1, refusal '%2')")
                  .arg(missing.found).arg(missing.refusal));

        const QString broken = QDir(here).filePath(QStringLiteral("failed_opens_broken.db"));
        removeLibrary(broken);
        exec(broken, "CREATE VIEW projects AS SELECT 'p-err' AS guid, abs(-9223372036854775807 - 1) AS scene");
        const openreads::Reads stepFailed = plan(broken, QStringLiteral("p-err"));
        check(!stepFailed.found && stepFailed.refusal.contains(QStringLiteral("could not be read")),
              QStringLiteral("2: a scene read whose step FAILS refuses the open by name (found %1, refusal '%2')")
                  .arg(stepFailed.found).arg(stepFailed.refusal));

        const QString unopenable = QDir(here).filePath(QStringLiteral("no-such-folder/failed_opens.db"));
        const openreads::Reads neverOpened = plan(unopenable, QStringLiteral("p-ok"));
        check(!neverOpened.found && neverOpened.refusal.contains(QStringLiteral("could not be read")),
              QStringLiteral("2: a read connection that NEVER OPENED refuses the open by name (found %1, refusal '%2')")
                  .arg(neverOpened.found).arg(neverOpened.refusal));
        check(!QFile::exists(unopenable), QStringLiteral("2: ...and creates no file"));
        removeLibrary(library);
        removeLibrary(broken);
    }

    // =======================================================================
    // 3. THE RESET's STEP AFTER ITS REBUILD (finding 1; the confirm's W3): the REAL step, libraryreset::
    //    keepAfterRebuild, handed every rebuild answer, the REAL sweep over a store holding one object no row names -
    //    what the reset DID for each: the kept store is collected (that object gone, the kept counts taken) ONLY after
    //    a rebuild that is ok, failed no row and committed; otherwise nothing is collected and the reset fails with
    //    the rebuild's own reason.
    // =======================================================================
    {
        const QString root = QDir(here).filePath(QStringLiteral("failed_opens_store3"));
        freshStore(root, /*withSidecars*/ true);
        const QString path = QDir(here).filePath(QStringLiteral("failed_opens_keep.db"));
        makeLibrary(path);
        Database library;
        library.initializeDatabase(path);
        const QString stray = AssetStorePaths::objectPathIn(root, QStringLiteral("aa30000000000000000000000000000000000000"),
                                                            QStringLiteral("png"));
        QDir().mkpath(QFileInfo(stray).absolutePath());
        int allowed = 0;
        for (const bool ok : { false, true })
            for (const int failedRows : { 0, 1 })
                for (const bool committed : { false, true }) {
                    {
                        QFile f(stray);
                        if (!f.open(QIODevice::WriteOnly) || f.write("no row names these bytes") <= 0)
                            check(false, QStringLiteral("3: setup: the stray object could not be written"));
                    }
                    AssetMigration::RebuildReport rebuilt;
                    rebuilt.ok = ok;
                    rebuilt.failedRows = failedRows;
                    rebuilt.committed = committed;
                    rebuilt.assets = 3;
                    rebuilt.error = QStringLiteral("the rebuild's own reason");
                    libraryreset::Result result;
                    libraryreset::keepAfterRebuild(&library, root, rebuilt, AssetMigration::RebuildOptions(), result);
                    const bool law = ok && failedRows == 0 && committed;
                    const bool swept = !QFile::exists(stray);
                    allowed += swept ? 1 : 0;
                    check(swept == law && result.kept.rows == (law ? 3 : 0)
                              && (law ? result.error.isEmpty() : result.error.contains(QStringLiteral("the rebuild's own reason"))),
                          QStringLiteral("3: rebuild ok %1, failed rows %2, committed %3: swept %4, kept rows %5, "
                                         "error '%6'").arg(ok).arg(failedRows).arg(committed).arg(swept)
                              .arg(result.kept.rows).arg(result.error));
                }
        check(allowed == 1, QStringLiteral("3: CONTROL: exactly one rebuild answer sweeps (%1)").arg(allowed));
        library.closeDatabase();
        removeLibrary(path);
    }

    // =======================================================================
    // 4. THE RESET's REAL ORDER: A LATER STEP FAILING AFTER A REBUILD THAT COMMITTED (the second confirm's finding 1).
    //    An older library over a store with one kept asset: the rebuild commits it, then the bootstrap's primitive
    //    seed fails - the library must read NOT this build's (0) and the next boot must reach the reset again, which,
    //    whole, is stamped this build's (the control). The stamp read back from the file.
    // =======================================================================
    {
        const QString root = QDir(here).filePath(QStringLiteral("failed_opens_store4"));
        freshStore(root, /*withSidecars*/ true);
        keptAsset(root, QStringLiteral("cd-kept4"), QStringLiteral("bb40000000000000000000000000000000000000"));
        const int older = librarygeneration::generation() - 1;
        const QString library = QDir(here).filePath(QStringLiteral("failed_opens_order.db"));
        makeLibrary(library);
        exec(library, "PRAGMA user_version = " + std::to_string(older));
        gResets = 0;
        gBootstrapError = true;
        const librarygeneration::Result failed = librarygeneration::checkAndWipe(library, here, nullptr);
        gBootstrapError = false;
        check(gResets == 1 && failed.outcome == librarygeneration::Outcome::Failed
                  && failed.reason.contains(QStringLiteral("primitive seed: injected")),
              QStringLiteral("4: CONTROL: the reset ran and FAILED at its later step (resets %1, outcome %2, '%3')")
                  .arg(gResets).arg(int(failed.outcome)).arg(failed.reason));
        check(generationOf(library) == 0,
              QStringLiteral("4: a reset that failed AFTER its rebuild committed leaves the library NOT this build's "
                             "(generation %1, want 0; this build %2)").arg(generationOf(library))
                  .arg(librarygeneration::generation()));
        gResets = 0;
        const librarygeneration::Result retried = librarygeneration::checkAndWipe(library, here, nullptr);
        check(gResets == 1 && retried.outcome == librarygeneration::Outcome::Wiped,
              QStringLiteral("4: ...so the next boot reaches the reset again (resets %1, outcome %2, '%3')")
                  .arg(gResets).arg(int(retried.outcome)).arg(retried.reason));
        check(generationOf(library) == librarygeneration::generation() && retried.keptRows == 1,
              QStringLiteral("4: CONTROL: the whole reset IS stamped this build's (generation %1, kept rows %2)")
                  .arg(generationOf(library)).arg(retried.keptRows));
        removeLibrary(library);
    }

    // =======================================================================
    // 5. A REFUSED CLEAR DATABASE ENDS IN NO DELETE (the second confirm's finding 1). A CURRENT library, Assets and
    //    Materials kept, a store holding a kept asset and one whose row the rebuild cannot write: the rebuild is
    //    refused; the reset seeds nothing after the refusal and leaves the library NOT this build's; the kept
    //    sidecar and its object survive the reset (its own forced sweep never ran) and an ordinary sweep after it.
    // =======================================================================
    {
        const QString root = QDir(here).filePath(QStringLiteral("failed_opens_store5"));
        freshStore(root, /*withSidecars*/ true);
        const QString keptObject = keptAsset(root, QStringLiteral("cd-kept"), QStringLiteral("cc50000000000000000000000000000000000000"));
        keptAsset(root, QStringLiteral("cd-poisoned"), QStringLiteral("dd50000000000000000000000000000000000000"));
        const QString keptSidecar = AssetStorePaths::sidecarPathIn(root, QStringLiteral("cd-kept"));
        const QString path = QDir(here).filePath(QStringLiteral("failed_opens_clear.db"));
        makeLibrary(path);
        Database library;
        library.initializeDatabase(path);
        libraryreset::Keep keep;
        keep.assets = true;
        keep.materials = true;
        libraryreset::Result result;
        gBootstraps = 0;
        sqlite3_auto_extension(reinterpret_cast<void (*)()>(poisonOnOpen));
        catalogSteps(&library, root, keep, result);
        sqlite3_cancel_auto_extension(reinterpret_cast<void (*)()>(poisonOnOpen));
        check(!result.ok && result.error.contains(QStringLiteral("cd-poisoned")),
              QStringLiteral("5: CONTROL: the rebuild was refused for the row it could not write (ok %1, '%2')")
                  .arg(result.ok).arg(result.error));
        check(gBootstraps == 0, QStringLiteral("5: a refused reset SEEDS NOTHING after the refusal (bootstraps %1)").arg(gBootstraps));
        check(QFile::exists(keptSidecar) && QFile::exists(keptObject),
              QStringLiteral("5: the kept sidecar and its object survive the refused reset (sidecar %1, object %2)")
                  .arg(QFile::exists(keptSidecar)).arg(QFile::exists(keptObject)));
        const AssetGc::Report ordinary = AssetGc::sweep(library.connection(), root, /*dryRun*/ false, /*force*/ false);
        check(QFile::exists(keptSidecar) && QFile::exists(keptObject),
              QStringLiteral("5: ...and an ORDINARY sweep after it (ok %1, '%2'; sidecar %3, object %4)")
                  .arg(ordinary.ok).arg(ordinary.error).arg(QFile::exists(keptSidecar)).arg(QFile::exists(keptObject)));
        // ...and a FORCED one (a user's assets.gc({force:true})): the pending reset refuses it by name.
        const AssetGc::Report forced = AssetGc::sweep(library.connection(), root, /*dryRun*/ false, /*force*/ true);
        check(!forced.ok && forced.error.contains(QStringLiteral("reset is not finished"))
                  && QFile::exists(keptSidecar) && QFile::exists(keptObject),
              QStringLiteral("5: ...and a FORCED sweep is refused while the reset is pending (ok %1, '%2'; sidecar %3, "
                             "object %4)").arg(forced.ok).arg(forced.error).arg(QFile::exists(keptSidecar))
                  .arg(QFile::exists(keptObject)));
        library.closeDatabase();
        check(generationOf(path) == 0,
              QStringLiteral("5: ...and the library is left NOT this build's, so the next boot retries the reset "
                             "(generation %1, want 0)").arg(generationOf(path)));
        removeLibrary(path);

        // A STORE WITH NO SIDECAR FOLDER (a Clear Database that kept nothing, crashed before its seed; the retry at
        // the next boot keeps Assets and Materials): nothing to rebuild is no failure - the reset is whole.
        const QString bare = QDir(here).filePath(QStringLiteral("failed_opens_store5b"));
        freshStore(bare, /*withSidecars*/ false);
        const QString path5b = QDir(here).filePath(QStringLiteral("failed_opens_bare.db"));
        makeLibrary(path5b);
        Database bareLibrary;
        bareLibrary.initializeDatabase(path5b);
        libraryreset::Result whole;
        gBootstraps = 0;
        catalogSteps(&bareLibrary, bare, keep, whole);
        check(whole.ok && gBootstraps == 1 && generationOf(path5b) == librarygeneration::generation(),
              QStringLiteral("5b: a kept reset over a store with no sidecar folder is WHOLE (ok %1 '%2', bootstraps %3, "
                             "generation %4)").arg(whole.ok).arg(whole.error).arg(gBootstraps).arg(generationOf(path5b)));
        // CONTROL for the guard: a library a whole reset stamped IS swept when forced (the stray object goes).
        const QString stray = AssetStorePaths::objectPathIn(bare, QStringLiteral("ee50000000000000000000000000000000000000"),
                                                            QStringLiteral("png"));
        QDir().mkpath(QFileInfo(stray).absolutePath());
        {
            QFile f(stray);
            check(f.open(QIODevice::WriteOnly) && f.write("stray") == 5, QStringLiteral("5b: the stray object is written"));
        }
        const AssetGc::Report swept = AssetGc::sweep(bareLibrary.connection(), bare, /*dryRun*/ false, /*force*/ true);
        check(swept.ok && !QFile::exists(stray),
              QStringLiteral("5b: CONTROL: a whole library's forced sweep runs (ok %1, '%2', stray left %3)")
                  .arg(swept.ok).arg(swept.error).arg(QFile::exists(stray)));
        bareLibrary.closeDatabase();
        removeLibrary(path5b);
    }


    // =======================================================================
    // 6. THE RESET's MARK IS DURABLE BEFORE STEP 2, A SCRIPT RUN's BATCH OPEN (the third confirm's finding 1): a write
    //    inside the editor's gesture batch, then the reset's first write - its 0 read back from a SECOND connection
    //    before step 2 could run, and the batch's own write committed with it (the control).
    // =======================================================================
    {
        const QString path = QDir(here).filePath(QStringLiteral("failed_opens_batch.db"));
        makeLibrary(path);
        Database library;
        library.initializeDatabase(path);
        library.beginBatch();
        const bool wrote = library.createProject(QStringLiteral("p6"), QStringLiteral("p6"));
        const bool batchLive = library.batchTransactionLive();
        check(wrote && batchLive, QStringLiteral("6: CONTROL: a write inside the batch, the batch holding its transaction "
                                                 "(wrote %1, live %2)").arg(wrote).arg(batchLive));
        libraryreset::Result marked;
        const bool ok = libraryreset::markPending(&library, marked);
        const int seen = generationOf(path);
        int projects = -1;
        {
            db::Options o;
            o.name = "failed-opens-second";
            db::Error e;
            db::Connection second = db::Connection::open(path.toStdString(), o, &e);
            db::Statement count(second, "SELECT COUNT(*) FROM projects WHERE guid = 'p6'");
            if (count.step(&e)) projects = int(count.int64(0));
        }
        check(ok && marked.pending && seen == 0,
              QStringLiteral("6: the reset's mark is COMMITTED before step 2, a batch open (marked %1, pending %2, "
                             "generation read by a second connection %3, want 0)").arg(ok).arg(marked.pending).arg(seen));
        check(projects == 1, QStringLiteral("6: CONTROL: the batch's own write was committed first (%1)").arg(projects));
        library.endBatch();
        library.closeDatabase();
        removeLibrary(path);
    }

    // =======================================================================
    // 7. A FAILED RESET OVER A PENDING LIBRARY ENDS THE SESSION, SAID ONE WAY (the third confirm's finding 2): on EVERY
    //    failing step - an earlier step's error (a project folder not removed), the wipe refused, the rebuild refused,
    //    the bootstrap's seed, the stamp refused - the reset answers `pending` (app.resetLibrary ends the session on
    //    it) and its error ends with the one sentence; a whole reset is not pending (the control).
    // =======================================================================
    {
        const QString root = QDir(here).filePath(QStringLiteral("failed_opens_store7"));
        const auto attempt = [&](const QString &name, const QString &earlier, int refuse, bool poison, bool seedError) {
            freshStore(root, /*withSidecars*/ true);
            keptAsset(root, QStringLiteral("cd-kept7"), QStringLiteral("ff70000000000000000000000000000000000000"));
            if (poison) keptAsset(root, QStringLiteral("cd-poisoned"), QStringLiteral("ff71000000000000000000000000000000000000"));
            const QString path = QDir(here).filePath(QStringLiteral("failed_opens_end.db"));
            makeLibrary(path);
            Database library;
            library.initializeDatabase(path);
            libraryreset::Keep keep;
            keep.assets = true;
            keep.materials = true;
            libraryreset::Result result;
            result.error = earlier;
            gRefuse = refuse;
            gBootstrapError = seedError;
            sqlite3_set_authorizer(libraryHandle(), refuseOnLibrary, nullptr);
            if (poison) sqlite3_auto_extension(reinterpret_cast<void (*)()>(poisonOnOpen));
            catalogSteps(&library, root, keep, result);
            if (poison) sqlite3_cancel_auto_extension(reinterpret_cast<void (*)()>(poisonOnOpen));
            sqlite3_set_authorizer(libraryHandle(), nullptr, nullptr);
            gRefuse = 0;
            gBootstrapError = false;
            library.closeDatabase();
            removeLibrary(path);
            check(name.isEmpty() ? (result.ok && !result.pending)
                                 : (!result.ok && result.pending && result.error.endsWith(libraryreset::unfinishedText())),
                  QStringLiteral("7: %1: ok %2, pending (the session ends) %3, '%4'")
                      .arg(name.isEmpty() ? QStringLiteral("CONTROL: a whole reset") : name)
                      .arg(result.ok).arg(result.pending).arg(result.error));
        };
        attempt(QStringLiteral("a step before the catalog failed"), QStringLiteral("a project folder could not be removed"),
                0, false, false);
        attempt(QStringLiteral("the wipe refused"), QString(), 1, false, false);
        attempt(QStringLiteral("the rebuild refused"), QString(), 0, true, false);
        attempt(QStringLiteral("the bootstrap's seed failed"), QString(), 0, false, true);
        attempt(QStringLiteral("the stamp refused"), QString(), 2, false, false);
        attempt(QString(), QString(), 0, false, false);
    }

    std::printf(failures ? "\n%d FAILURES\n" : "\nall failed-open assertions passed\n", failures);
    return failures ? 1 : 0;
}
