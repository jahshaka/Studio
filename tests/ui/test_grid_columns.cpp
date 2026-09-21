/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.grid_columns — THE ASSET GRID LAYS OUT BEFORE IT HAS EVER BEEN RESIZED
// (UNINIT-SWEEP-1, hazard A1-4).
//
// `AssetViewGrid::lastWidth` is written in exactly one place — resizeEvent —
// and read by three others that turn it into a column count: searchTiles(),
// filterAssets() and removeItem()'s updateGridColumns(lastWidth). The tray is
// POPULATED BEFORE IT IS SHOWN, so all three are reachable before any resize
// has happened, and until this lane `lastWidth` was uninitialised: the column
// count on that path was whatever the heap held under the widget.
//
// THE HONEST VALUE IS ZERO, and zero is the one value the old code could not
// survive: updateGridColumns() carried `if (columnCount == 0) columnCount = 1`
// and the two others, which spelled the same division out again, did not — so
// an honest `lastWidth = 0` would have turned a garbage layout into an integer
// DIVISION BY ZERO the first time anyone searched or filtered a tray that had
// not been shown yet. The column count is one function now (columnsFor) and
// all five sites call it.
//
// WHAT THIS ASSERTS, with no window ever shown and no resize ever delivered:
//   * a search that matches, a search that clears, and a filter by drawer all
//     complete and lay the tiles out in ONE column — the same layout a grid
//     too narrow for a single tile has always produced;
//   * removing a tile from an unresized grid re-lays the rest;
//   * and the arithmetic itself: columnsFor() never answers zero.
//
// MALLOC_PERTURB_ is set on the suite so that, on a tree WITHOUT the
// initialiser, `lastWidth` reads a large non-zero constant rather than
// whatever happens to be there — the failure is then the wrong column count,
// deterministically, instead of a coin. (That variable is glibc's allocator
// fill; it is NOT MALLOC_CHECK_, which needs libc_malloc_debug preloaded on
// this box.)

#include <QApplication>
#include <QGridLayout>
#include <QJsonObject>
#include <cstdio>

#include "ui/controls/assetviewgrid.h"
#include "ui/controls/assetgriditem.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

/// How many columns the VISIBLE tiles occupy. The grid numbers its columns
/// from 1, and a search leaves the tiles it hid where they were — what the
/// call laid out is the set it made visible.
int columnsUsed(QGridLayout *layout)
{
    int widest = 0;
    for (int i = 0; i < layout->count(); ++i) {
        QWidget *w = layout->itemAt(i) ? layout->itemAt(i)->widget() : nullptr;
        if (!w || w->isHidden()) continue;
        int row = 0, col = 0, rs = 0, cs = 0;
        layout->getItemPosition(i, &row, &col, &rs, &cs);
        if (col > widest) widest = col;
    }
    return widest;
}

AssetGridItem *addTile(AssetViewGrid &grid, const QString &name, int collection)
{
    QJsonObject details;
    details.insert(QStringLiteral("guid"), name);
    details.insert(QStringLiteral("name"), name);
    details.insert(QStringLiteral("collection"), collection);
    details.insert(QStringLiteral("full_filename"), name);
    grid.addTo(details, QImage(), grid.tiles().size(), QJsonObject(), QJsonObject());
    return grid.tiles().isEmpty() ? nullptr : grid.tiles().last();
}

}   // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // The arithmetic, at the two ends and in the middle. A grid narrower than
    // one tile still has one column; a never-resized grid (width 0) is that
    // case, which is the whole point.
    CHECK(AssetViewGrid::columnsFor(0) == 1,
          "columnsFor(0) is one column, not zero (the never-resized grid)");
    CHECK(AssetViewGrid::columnsFor(1) == 1, "a grid narrower than one tile is one column");
    CHECK(AssetViewGrid::columnsFor(137) == 1, "one tile short of two tiles is one column");
    CHECK(AssetViewGrid::columnsFor(1920) == 13, "a 1920 px grid is thirteen columns");

    AssetViewGrid grid(nullptr);
    CHECK(grid.lastWidth == 0,
          "a grid that has never been resized reports a width of zero, not a heap value");

    for (int i = 0; i < 5; ++i)
        addTile(grid, QStringLiteral("tile%1").arg(i), i < 3 ? 1 : 2);
    CHECK(grid.tiles().size() == 5, "five tiles populated before the grid was ever shown");

    // NO resizeEvent is delivered anywhere in this test — that is the case.
    grid.searchTiles(QStringLiteral("tile1"));
    CHECK(columnsUsed(grid._layout) == 1,
          "a search on a never-resized grid lays its match out in one column");

    grid.searchTiles(QString());
    CHECK(columnsUsed(grid._layout) == 1,
          "clearing the search re-lays every tile in one column");
    CHECK(grid.tiles().size() == 5, "...and loses none of them");

    grid.filterAssets(1);
    CHECK(columnsUsed(grid._layout) == 1,
          "filtering by drawer on a never-resized grid lays out in one column");

    grid.filterAssets(-1);
    CHECK(columnsUsed(grid._layout) == 1, "and so does clearing the filter");

    AssetGridItem *doomed = grid.tiles().last();
    grid.deleteTile(doomed);
    CHECK(grid.tiles().size() == 4,
          "a tile removed from a never-resized grid takes the others' layout with it");
    CHECK(columnsUsed(grid._layout) == 1, "...still one column");

    std::printf("%s\n", failures ? "ui.grid_columns: FAILED" : "ui.grid_columns: PASSED");
    return failures ? 1 : 0;
}
