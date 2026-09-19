// thumbnails.sweep — THE REPAIR SWEEP's contract, with no engine and no window
// (THUMBS-1 fix round F1/F2/F6).
//
// Three things it must get right, none of which a picture can show:
//
//   1. IT STOPS WHEN ASKED. The sweep yields to the event loop between assets,
//      and a yield delivers the WINDOW'S CLOSE: MainWindow::closeEvent runs with
//      the sweep on the stack and shutdownBackgroundWork destroys the thumbnail
//      renderer. Without a stop the loop would carry on afterwards — re-creating
//      the renderer, rendering the rest of the library with the window gone, and
//      finally opening a modal box on a dead window (the "modal swallowed the
//      quit" zombie, ended only by the 20 s force-exit). So: a stop request
//      makes the sweep stop at its next row, say so in `cancelled`, and NEVER
//      create a renderer afterwards.
//
//   2. IT DOES NOT LOAD THE LIBRARY INTO MEMORY TO DECIDE. The row list is
//      (guid, type, has-a-thumbnail) straight from SQL; `missingOnly` is a
//      WHERE clause, not a decode of every stored PNG.
//
//   3. "NOTHING TO DRAW" IS NOT "BROKEN". A builtin primitive's Object row (the
//      default Ground) stores no definition and is SKIPPED; a row that has a
//      definition and cannot be drawn is a FAILURE with a reason.
//
// Real Database on a throwaway SQLite file; a null engine, which is exactly
// what a headless session hands the sweep.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <memory>

#include "data/database/database.h"
#include "data/project.h"
#include "bridge/enginethumbnailrenderer.h"
#include "services/thumbnailrebuild.h"
#include "services/thumbnailstop.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static void addRow(Database &db, const QString &guid, const char *name, ModelTypes type,
                   const QByteArray &thumbnail, const QByteArray &definition)
{
    db.createAssetEntry(guid, QString::fromUtf8(name), static_cast<int>(type), QString(),
                        QString(), QString(), QString(), thumbnail, QByteArray(), QByteArray(),
                        definition);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);

    const QString dbPath = QDir::current().filePath("test_thumbnail_sweep.db");
    QFile::remove(dbPath);
    Database db;
    CHECK(db.initializeDatabase(dbPath), "throwaway database opened");
    db.createAllTables();

    // A library with one of each shape the sweep has to tell apart.
    const QByteArray png("\x89PNG-not-really", 15);   // non-empty: "has a thumbnail"
    addRow(db, "has-a-tile", "Model With A Tile", ModelTypes::Object, png, QByteArray("{}"));
    addRow(db, "builtin-ground", "Ground", ModelTypes::Object, QByteArray(), QByteArray());
    addRow(db, "broken-model", "Model With No Bytes", ModelTypes::Object, QByteArray(),
           QByteArray("{\"name\":\"gone\"}"));
    addRow(db, "a-mesh-member", "member.glb", ModelTypes::Mesh, QByteArray(), QByteArray());

    // ---- 2. the row list is (guid, type, has-a-thumbnail), from SQL --------
    {
        const auto all = db.fetchAssetThumbnailStates(/*missingOnly=*/false);
        CHECK(all.size() == 4, "every row is listed, whatever its view filter");
        const auto missing = db.fetchAssetThumbnailStates(/*missingOnly=*/true);
        CHECK(missing.size() == 3, "missingOnly is a WHERE clause: the row with a blob is not in it");
        bool tileSeen = false;
        for (const auto &row : all)
            if (row.guid == QStringLiteral("has-a-tile")) tileSeen = row.hasThumbnail;
        CHECK(tileSeen, "…and the full list still says WHICH rows have one");
    }

    // ---- 1. a stop request stops the sweep, and nothing is rendered --------
    {
        CHECK(!EngineThumbnailRenderer::exists(), "no thumbnail renderer exists yet");
        thumbrebuild::requestStop();   // what ThumbnailGenerator::shutdown() does
        thumbrebuild::SweepOptions options;
        const auto result = thumbrebuild::rebuildMissing(&db, nullptr, nullptr, options);
        CHECK(result.cancelled, "a stop requested before the sweep CANCELS it");
        CHECK(result.rebuilt == 0 && result.considered == 0,
              "…at the first row: nothing was considered, nothing rebuilt");
        CHECK(thumbrebuild::stopRequested(),
              "…and the stop is STICKY: a sweep does not clear it on its way past");
        CHECK(!EngineThumbnailRenderer::exists(),
              "…and NO renderer was created after the shutdown that asked us to stop");
    }

    // A stop arriving mid-sweep. The predicate is consulted ONCE PER ROW —
    // not per yield, which a healthy library only reaches every sixteenth row
    // (F2) — so a stop is acted on at the next asset either way.
    {
        thumbrebuild::clearStop();
        int asked = 0;
        thumbrebuild::SweepOptions options;
        options.cancelled = [&asked] { return ++asked > 1; };   // stop at the second row
        const auto result = thumbrebuild::rebuildMissing(&db, nullptr, nullptr, options);
        CHECK(result.cancelled, "a stop that arrives DURING the sweep stops it too");
        CHECK(asked == 2, "…the stop source is asked once per row, not once per yield");
        CHECK(result.considered == 1, "…and the sweep stopped at the row it was asked at");
    }

    // ---- 3. skipped is not failed -----------------------------------------
    {
        thumbrebuild::clearStop();
        thumbrebuild::SweepOptions options;
        const auto result = thumbrebuild::rebuildMissing(&db, nullptr, nullptr, options);
        CHECK(!result.cancelled, "with no stop outstanding the sweep runs to the end");
        // missingOnly: the row that HAS a blob never reaches the loop (SQL), and
        // the Mesh member is not a tile — so two rows are considered, not four.
        CHECK(result.considered == 2,
              QString("only the missing, drawable rows are considered (%1)")
                  .arg(result.considered).toUtf8().constData());
        CHECK(result.skipped == 1,
              QString("the builtin row with no definition is SKIPPED (%1)")
                  .arg(result.skipped).toUtf8().constData());
        CHECK(result.failed.size() == 1,
              QString("the row that HAS a definition and cannot be drawn is a FAILURE (%1)")
                  .arg(result.failed.size()).toUtf8().constData());
        bool named = false;
        for (const auto &failure : result.failed) {
            std::printf("    %s: %s\n", qUtf8Printable(failure.guid), qUtf8Printable(failure.reason));
            if (failure.guid == QStringLiteral("broken-model")) named = !failure.reason.isEmpty();
        }
        CHECK(named, "…each with a reason naming what is wrong");
        CHECK(result.rebuilt == 0, "nothing was rebuilt without an engine");
    }

    // A row with a definition but no engine says so, and is not a skip.
    {
        const auto outcome = thumbrebuild::rebuildOne(&db, nullptr, "broken-model", nullptr);
        CHECK(!outcome.ok, "rebuildOne on a model with no engine refuses");
        CHECK(!outcome.nothingToDraw,
              "…as a FAILURE, not as 'nothing to draw' (the row has a definition)");
        std::printf("    reason: %s\n", qUtf8Printable(outcome.reason));
        const auto builtin = thumbrebuild::rebuildOne(&db, nullptr, "builtin-ground", nullptr);
        CHECK(builtin.nothingToDraw, "…while the builtin row IS 'nothing to draw'");
    }

    db.closeDatabase();
    QFile::remove(dbPath);
    std::printf("%s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
