// Headless characterisation test for the multiple-desktops storage layer
// (DESKTOPS_SPEC.md): the guarded projects-table migration, the desktop filter
// on fetchProjects, the move round-trip, and the freeform position round-trip.
//
// Builds the REAL Database class (src/core/database/database.cpp) against a
// throwaway SQLite file in the test working directory — the user's live
// JahLibrary.db is never touched. Runs under QT_QPA_PLATFORM=offscreen.
// Framework-free; non-zero exit on failure.
#include <QApplication>
#include <QFile>
#include <QSqlQuery>
#include <QSqlError>
#include <cmath>
#include <cstdio>

#include "core/database/database.h"
#include "core/project.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

// find a tile by guid in a fetch result; null name means "not found"
static ProjectTileData findTile(const QVector<ProjectTileData> &tiles, const QString &guid)
{
    for (const auto &t : tiles) if (t.guid == guid) return t;
    return ProjectTileData();
}

// Optional second mode: test_desktops_db <path-to-library-copy>
// Runs the startup migration against a COPY of a real JahLibrary.db and verifies
// nothing is lost: all projects present, every one on Desktop 1, positions NULL.
static int migrateLibraryCopy(const QString &path)
{
    Database db;
    CHECK(db.initializeDatabase(path), "library copy opened");

    const int before = [] {
        QSqlQuery q;
        q.exec("SELECT COUNT(*) FROM projects");
        return q.next() ? q.value(0).toInt() : -1;
    }();
    printf("info: library copy has %d project(s)\n", before);

    db.createAllTables();   // the migration the real app runs at startup

    CHECK(db.checkIfColumnExists("projects", "desktop"),   "copy migrated: desktop column");
    CHECK(db.checkIfColumnExists("projects", "desktop_x"), "copy migrated: desktop_x column");
    CHECK(db.checkIfColumnExists("projects", "desktop_y"), "copy migrated: desktop_y column");

    auto all = db.fetchProjects();
    auto d1  = db.fetchProjects(1);
    CHECK(all.size() == before, "no project lost by the migration");
    CHECK(d1.size() == before, "every existing project lands on Desktop 1");
    for (const auto &t : d1) {
        if (t.desktop != 1 || t.hasPosition) {
            printf("FAIL: project %s desktop=%d hasPosition=%d\n",
                   qPrintable(t.guid), t.desktop, int(t.hasPosition));
            ++failures;
        }
    }
    CHECK(true, "migrated projects default to desktop 1, unplaced");

    db.closeDatabase();
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("library copy migrated cleanly\n");
    return 0;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);   // database.cpp links QtWidgets (QMessageBox)

    if (argc > 1) return migrateLibraryCopy(QString::fromLocal8Bit(argv[1]));

    const QString dbPath = "desktops_test.db";
    QFile::remove(dbPath);

    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");

    // --- Simulate a PRE-DESKTOPS library: the projects table as it shipped before
    //     the desktop columns existed, with two legacy projects on it.
    {
        QSqlQuery q;
        bool ok = q.exec(
            "CREATE TABLE projects ("
            "    name VARCHAR(64), last_accessed DATETIME, last_written DATETIME,"
            "    date_created DATETIME DEFAULT CURRENT_TIMESTAMP, version VARCHAR(8),"
            "    description TEXT, url TEXT, guid VARCHAR(32) PRIMARY KEY,"
            "    thumbnail BLOB, scene BLOB)");
        CHECK(ok, "legacy (pre-desktops) projects table created");

        ok = q.exec("INSERT INTO projects (name, last_written, guid) VALUES "
                    "('Legacy A', datetime('now', '-1 hour'), 'guid-a'),"
                    "('Legacy B', datetime('now', '-2 hour'), 'guid-b')");
        CHECK(ok, "two legacy projects inserted");
    }
    CHECK(!db.checkIfColumnExists("projects", "desktop"), "precondition: no desktop column yet");

    // --- The startup migration (createAllTables runs migrateProjectsTable)
    db.createAllTables();
    CHECK(db.checkIfColumnExists("projects", "desktop"),   "migration added the desktop column");
    CHECK(db.checkIfColumnExists("projects", "desktop_x"), "migration added the desktop_x column");
    CHECK(db.checkIfColumnExists("projects", "desktop_y"), "migration added the desktop_y column");

    // --- Legacy rows belong to Desktop 1 and are unplaced
    {
        auto tiles = db.fetchProjects(1);
        CHECK(tiles.size() == 2, "both legacy projects show on Desktop 1");
        auto a = findTile(tiles, "guid-a");
        CHECK(a.desktop == 1, "legacy project reads back as desktop 1");
        CHECK(!a.hasPosition, "legacy project has no freeform position (NULL)");
        CHECK(db.fetchProjects(2).isEmpty(), "Desktop 2 starts empty");
        CHECK(db.fetchProjects(3).isEmpty(), "Desktop 3 starts empty");
        CHECK(db.fetchProjects().size() == 2, "unfiltered fetch (legacy call) still returns everything");
    }

    // --- Idempotence: a second startup must not duplicate columns or lose rows
    db.createAllTables();
    CHECK(db.fetchProjects().size() == 2, "running the migration twice is harmless");

    // --- Move round-trip: right-click -> Move to Desktop 3
    CHECK(db.updateProjectDesktop("guid-a", 3), "updateProjectDesktop succeeds");
    {
        auto d3 = db.fetchProjects(3);
        auto d1 = db.fetchProjects(1);
        CHECK(d3.size() == 1 && d3[0].guid == "guid-a", "moved project shows on Desktop 3");
        CHECK(d3[0].desktop == 3, "moved project reads back desktop 3");
        CHECK(d1.size() == 1 && d1[0].guid == "guid-b", "moved project left Desktop 1");
    }

    // --- New projects default to Desktop 1 via the schema DEFAULT
    CHECK(db.createProject("guid-c", "Fresh"), "createProject on the migrated table");
    {
        auto c = findTile(db.fetchProjects(1), "guid-c");
        CHECK(c.guid == "guid-c" && c.desktop == 1, "new project lands on Desktop 1 by default");
    }

    // --- Freeform position round-trip (normalized 0..1)
    CHECK(db.updateProjectPosition("guid-a", 0.25f, 0.75f), "updateProjectPosition succeeds");
    {
        auto a = findTile(db.fetchProjects(3), "guid-a");
        CHECK(a.hasPosition, "placed project reads back hasPosition");
        CHECK(std::fabs(a.posX - 0.25f) < 1e-5f && std::fabs(a.posY - 0.75f) < 1e-5f,
              "stored position round-trips exactly");
        auto b = findTile(db.fetchProjects(1), "guid-b");
        CHECK(!b.hasPosition, "unplaced project still reports no position");
    }

    // --- A row written with an explicit NULL desktop (e.g. by an older build after a
    //     downgrade) must still show up on Desktop 1
    {
        QSqlQuery q;
        q.exec("INSERT INTO projects (name, last_written, guid, desktop) "
               "VALUES ('Downgrade', datetime(), 'guid-d', NULL)");
        auto d = findTile(db.fetchProjects(1), "guid-d");
        CHECK(d.guid == "guid-d" && d.desktop == 1, "NULL desktop value reads as Desktop 1");
    }

    db.closeDatabase();
    QFile::remove(dbPath);

    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("all desktops DB checks passed\n");
    return 0;
}
