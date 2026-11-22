/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// desktops.grid_lookup — THE DESKTOP FINDS A TILE BY ITS GUID, EXACTLY, AT 500 PROJECTS (LARGE-LIBRARY-1
// stage 1, the spec's call 6). The REAL DynamicGrid (its tiles by guid, a hash beside `originalItems`) and
// the REAL PendingTiles (the rows still waiting for their slice, ui/controls/pendingtiles.h), each driven
// through every operation that changes its set, and after each one EVERY guid is asked and compared with a
// plain reference model of what should be there:
//   the grid: 500 added, insertTileAtHead, moveTileToHead, a deletion, resetView (and a rebuild after it);
//   the pending rows: a reset of 500, a slice taken (32), removals in the middle and at the head, a rename,
//   every slice taken to the end (in listing order, the removed rows skipped).
// A correctness check, no visit counter (the lead's ruling F6). Headless (offscreen), no display.

#include <QApplication>
#include <QHash>
#include <QSet>
#include <QUuid>
#include <cstdio>

#include "data/project.h"
#include "ui/controls/dynamicgrid.h"
#include "ui/controls/itemgridwidget.h"
#include "ui/controls/pendingtiles.h"

static int failures = 0;
#define CHECK(cond, ...)                                                                   \
    do {                                                                                   \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); }  \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } \
        std::fflush(stdout);                                                               \
    } while (0)

static ProjectTileData row(int i)
{
    ProjectTileData r;
    r.guid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.name = QStringLiteral("Project %1").arg(i, 4, 10, QLatin1Char('0'));
    return r;
}

/// Every guid ever seen, asked of the grid: the tile showing it (with that guid) when `live` holds it, null
/// when not. The count of wrong answers.
static int gridMisses(DynamicGrid &grid, const QStringList &everSeen, const QSet<QString> &live)
{
    int misses = 0;
    for (const QString &guid : everSeen) {
        ItemGridWidget *tile = grid.tile(guid);
        if (live.contains(guid)) misses += (tile && tile->tileData.guid == guid) ? 0 : 1;
        else misses += tile ? 1 : 0;
    }
    return misses + (grid.originalItems.size() == live.size() ? 0 : 1);
}

static int pendingMisses(PendingTiles &pending, const QStringList &everSeen, const QHash<QString, QString> &live)
{
    int misses = 0;
    for (const QString &guid : everSeen) {
        ProjectTileData *r = pending.find(guid);
        if (live.contains(guid)) misses += (r && r->guid == guid && r->name == live.value(guid) && pending.contains(guid)) ? 0 : 1;
        else misses += (r || pending.contains(guid)) ? 1 : 0;
    }
    return misses + (pending.size() == live.size() ? 0 : 1);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    const int P = 500;

    // ---- THE GRID ---------------------------------------------------------------------------------------
    {
        DynamicGrid grid;
        QStringList everSeen;
        QSet<QString> live;
        QVector<ProjectTileData> rows;
        for (int i = 0; i < P; ++i) {
            rows.append(row(i));
            grid.addToGridView(rows.last());
            everSeen << rows.last().guid;
            live.insert(rows.last().guid);
        }
        CHECK(gridMisses(grid, everSeen, live) == 0, "the grid finds each of %d tiles by its guid", P);

        const ProjectTileData head = row(P);
        grid.insertTileAtHead(head, false);
        everSeen << head.guid;
        live.insert(head.guid);
        CHECK(grid.originalItems.first()->tileData.guid == head.guid && gridMisses(grid, everSeen, live) == 0,
              "insertTileAtHead: the new tile is first and every guid still finds its tile");

        grid.moveTileToHead(grid.tile(rows.at(250).guid));
        CHECK(grid.originalItems.first()->tileData.guid == rows.at(250).guid && gridMisses(grid, everSeen, live) == 0,
              "moveTileToHead: the moved tile is first and every guid still finds its tile");

        grid.deleteTile(grid.tile(rows.at(100).guid));
        live.remove(rows.at(100).guid);
        CHECK(!grid.tile(rows.at(100).guid) && gridMisses(grid, everSeen, live) == 0,
              "a deletion: the deleted guid finds nothing, every other guid its tile");

        grid.resetView();
        live.clear();
        CHECK(gridMisses(grid, everSeen, live) == 0, "resetView: no guid finds a tile (%d asked)", int(everSeen.size()));

        for (int i = 0; i < 10; ++i) {
            grid.addToGridView(rows.at(i));
            live.insert(rows.at(i).guid);
        }
        CHECK(gridMisses(grid, everSeen, live) == 0, "a rebuild after the reset: exactly the 10 rebuilt guids find tiles");
        QCoreApplication::processEvents();   // the deleted tile's deleteLater
    }

    // ---- THE PENDING ROWS -------------------------------------------------------------------------------
    {
        PendingTiles pending;
        QVector<ProjectTileData> rows;
        QStringList everSeen, order;
        QHash<QString, QString> live;
        for (int i = 0; i < P; ++i) {
            rows.append(row(i));
            everSeen << rows.last().guid;
            order << rows.last().guid;
            live.insert(rows.last().guid, rows.last().name);
        }
        pending.reset(rows);
        CHECK(pendingMisses(pending, everSeen, live) == 0, "the pending rows find each of %d guids", P);

        bool inOrder = true;
        for (int i = 0; i < 32; ++i) {
            const ProjectTileData r = pending.takeFirst();
            inOrder &= r.guid == order.takeFirst();
            live.remove(r.guid);
        }
        CHECK(inOrder && pendingMisses(pending, everSeen, live) == 0,
              "a slice of 32 is taken in listing order; every guid still answers exactly");

        for (int i : { 33, 200, 201, 499 }) {
            CHECK(pending.remove(rows.at(i).guid), "row %d is removed while waiting", i);
            live.remove(rows.at(i).guid);
            order.removeOne(rows.at(i).guid);
        }
        CHECK(!pending.remove(rows.at(0).guid), "a row already built is not waiting: removing it answers false");
        CHECK(pendingMisses(pending, everSeen, live) == 0, "after the removals every guid answers exactly");

        pending.find(rows.at(300).guid)->name = QStringLiteral("Renamed");
        live[rows.at(300).guid] = QStringLiteral("Renamed");
        CHECK(pendingMisses(pending, everSeen, live) == 0, "a rename lands on the waiting row");

        int taken = 0;
        while (!pending.isEmpty()) {
            const ProjectTileData r = pending.takeFirst();
            inOrder &= !order.isEmpty() && r.guid == order.takeFirst();
            live.remove(r.guid);
            ++taken;
        }
        CHECK(inOrder && order.isEmpty() && taken == P - 32 - 4 && pendingMisses(pending, everSeen, live) == 0,
              "every slice taken to the end: %d rows in listing order, the removed ones skipped, nothing left", taken);
    }

    if (failures) std::printf("desktops.grid_lookup: FAILED (%d)\n", failures);
    else std::printf("desktops.grid_lookup: PASSED\n");
    return failures ? 1 : 0;
}
