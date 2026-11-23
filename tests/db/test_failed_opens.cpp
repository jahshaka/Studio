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
//   4. a reset that failed stamps nothing (the confirm's F7): the library keeps its generation, so the next boot
//      retries; only a reset that answered ok is stamped this build's (the stamp read back from the file).
//
// THE LINK, test-side only: libraryreset::reset is a RECORDER here (the reset's own closure is the
// whole import and seed machinery; what this row proves is the DECISION to call it), AssetGc::sweep and
// AssetMigration::keptObjects are recorders (case 3), and SceneReader's two source collectors answer empty
// lists (readPlan calls them only for a scene that was found).

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>

#include <cstdio>
#include <string>

#include "data/database/database.h"
#include "data/db/connection.h"
#include "data/db/statement.h"
#include "io/scenereader.h"
#include "services/assetgc.h"
#include "services/assetmigration.h"
#include "services/librarygeneration.h"
#include "services/libraryreset.h"
#include "services/openreads.h"

namespace {

int failures = 0;
int gResets = 0;
bool gResetRefuses = false;   // the recorder answers a reset refused for a row it could not rebuild (case 4)
int gSweeps = 0;              // the store's sweeps the reset's step after its rebuild ran (case 3)

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

}   // namespace

// ---- the recorders (above) ------------------------------------------------------------------------
namespace libraryreset {
QString refusalReason(const PreviewService *) { return QString(); }
Result reset(Database *, SettingsManager *, const QString &, const std::function<QString(const QString &)> &, bool,
             const Keep &)
{
    ++gResets;
    Result result;
    result.ok = !gResetRefuses;
    if (gResetRefuses)
        result.error = QStringLiteral("the kept storages could not be rebuilt: 1 stored item(s) could not be rebuilt (f7)");
    return result;
}
}   // namespace libraryreset

// THE STORE's TWO, for case 3: the reset's step after its rebuild calls these (the sweep is what it DID).
namespace AssetGc {
Report sweep(db::Connection &, const QString &, bool, bool)
{
    ++gSweeps;
    Report report;
    report.ok = true;
    return report;
}
}   // namespace AssetGc
namespace AssetMigration {
QSet<QString> keptObjects(const QString &, const RebuildOptions &) { return {}; }
}   // namespace AssetMigration

QStringList SceneReader::collectMeshSources(const QJsonObject &) { return {}; }
QStringList SceneReader::collectClipSources(const QJsonObject &) { return {}; }

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    const QString here = QDir::currentPath();

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
    //    keepAfterRebuild, handed every rebuild answer, the sweep a recorder - what the reset DID for each: the kept
    //    store is collected (and the kept counts taken) ONLY after a rebuild that is ok, failed no row and committed;
    //    otherwise nothing is collected and the reset fails with the rebuild's own reason.
    // =======================================================================
    {
        Database unopened;   // the recorder never touches its connection
        int allowed = 0;
        for (const bool ok : { false, true })
            for (const int failedRows : { 0, 1 })
                for (const bool committed : { false, true }) {
                    AssetMigration::RebuildReport rebuilt;
                    rebuilt.ok = ok;
                    rebuilt.failedRows = failedRows;
                    rebuilt.committed = committed;
                    rebuilt.assets = 3;
                    rebuilt.error = QStringLiteral("the rebuild's own reason");
                    libraryreset::Result result;
                    gSweeps = 0;
                    libraryreset::keepAfterRebuild(&unopened, here, rebuilt, AssetMigration::RebuildOptions(), result);
                    const bool law = ok && failedRows == 0 && committed;
                    allowed += gSweeps;
                    check(gSweeps == (law ? 1 : 0) && result.kept.rows == (law ? 3 : 0)
                              && (law ? result.error.isEmpty() : result.error.contains(QStringLiteral("the rebuild's own reason"))),
                          QStringLiteral("3: rebuild ok %1, failed rows %2, committed %3: swept %4 time(s), kept rows %5, "
                                         "error '%6'").arg(ok).arg(failedRows).arg(committed).arg(gSweeps)
                              .arg(result.kept.rows).arg(result.error));
                }
        check(allowed == 1, QStringLiteral("3: CONTROL: exactly one rebuild answer sweeps (%1)").arg(allowed));
    }

    // =======================================================================
    // 4. A RESET THAT FAILED STAMPS NOTHING (the confirm's F7): the library keeps the generation it had, so the next
    //    boot reads it as older and retries - and only a reset that answered ok is stamped this build's (the stamp
    //    read back from the file, the reset's answer the recorder's).
    // =======================================================================
    {
        const auto generationOf = [](const QString &path) {
            db::Options o;
            o.name = "failed-opens-generation";
            db::Error e;
            db::Connection c = db::Connection::open(path.toStdString(), o, &e);
            db::Statement s(c, "PRAGMA user_version");
            return s.step(&e) ? int(s.int64(0)) : -1;
        };
        const int older = librarygeneration::generation() - 1;
        const QString library = QDir(here).filePath(QStringLiteral("failed_opens_f7.db"));
        makeLibrary(library);
        exec(library, "PRAGMA user_version = " + std::to_string(older));
        gResets = 0;
        gResetRefuses = true;
        const librarygeneration::Result refused = librarygeneration::checkAndWipe(library, here, nullptr);
        gResetRefuses = false;
        check(gResets == 1 && refused.outcome == librarygeneration::Outcome::Failed && generationOf(library) == older,
              QStringLiteral("4: a reset REFUSED for a row it could not rebuild stamps nothing (resets %1, outcome %2, "
                             "generation %3, was %4)").arg(gResets).arg(int(refused.outcome)).arg(generationOf(library)).arg(older));
        gResets = 0;
        const librarygeneration::Result wiped = librarygeneration::checkAndWipe(library, here, nullptr);
        check(gResets == 1 && wiped.outcome == librarygeneration::Outcome::Wiped
                  && generationOf(library) == librarygeneration::generation(),
              QStringLiteral("4: a reset that answered ok IS stamped this build's (resets %1, outcome %2, generation %3, "
                             "this build %4)").arg(gResets).arg(int(wiped.outcome)).arg(generationOf(library))
                  .arg(librarygeneration::generation()));
        removeLibrary(library);
    }

    std::printf(failures ? "\n%d FAILURES\n" : "\nall failed-open assertions passed\n", failures);
    return failures ? 1 : 0;
}
