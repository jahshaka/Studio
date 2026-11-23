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
//   3. the library reset's sweep after its rebuild (the audit's finding 1): libraryreset::sweepAllowedAfter's
//      whole truth table - the kept store is collected only after a rebuild that is ok, failed no row and
//      committed (the call site, libraryreset.cpp, is that one line).
//
// THE LINK, test-side only: libraryreset::reset is a RECORDER here (the reset's own closure is the
// whole import and seed machinery; what this row proves is the DECISION to call it), and SceneReader's
// two source collectors answer empty lists (readPlan calls them only for a scene that was found).

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
#include "services/librarygeneration.h"
#include "services/libraryreset.h"
#include "services/openreads.h"

namespace {

int failures = 0;
int gResets = 0;

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
    result.ok = true;
    return result;
}
}   // namespace libraryreset

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
    // 3. THE RESET's SWEEP AFTER ITS REBUILD (finding 1's decision, libraryreset::sweepAllowedAfter): its whole
    //    truth table - the kept store is collected ONLY after a rebuild that is ok, failed no row and committed.
    // =======================================================================
    {
        int allowed = 0;
        for (const bool ok : { false, true })
            for (const int failedRows : { 0, 1 })
                for (const bool committed : { false, true }) {
                    const bool sweep = libraryreset::sweepAllowedAfter(ok, failedRows, committed);
                    const bool law = ok && failedRows == 0 && committed;
                    allowed += sweep ? 1 : 0;
                    check(sweep == law, QStringLiteral("3: rebuild ok %1, failed rows %2, committed %3: the sweep %4")
                                            .arg(ok).arg(failedRows).arg(committed)
                                            .arg(sweep ? QStringLiteral("RUNS") : QStringLiteral("is refused")));
                }
        check(allowed == 1, QStringLiteral("3: CONTROL: exactly one row of the table sweeps (%1)").arg(allowed));
    }

    std::printf(failures ? "\n%d FAILURES\n" : "\nall failed-open assertions passed\n", failures);
    return failures ? 1 : 0;
}
