// Unit tests for the SLIDER-mode layout model (DESKTOP_SLIDER_SPEC.md):
// round-robin seeding from rows order, freeform y-band seeding, lossless
// assignment round-trips, insert-at-drop ordering, clamping, and the
// session-only row offsets. Pure model — no widgets, no database.
// Framework-free; non-zero exit on failure.
#include <QString>
#include <QVector>
#include <cstdio>

#include "ui/controls/sliderlayoutmodel.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

static SliderTileInfo plain(const QString &guid)
{
    SliderTileInfo t; t.guid = guid; return t;
}

static SliderTileInfo assigned(const QString &guid, int row, int index)
{
    SliderTileInfo t; t.guid = guid; t.hasSlider = true; t.row = row; t.index = index; return t;
}

static SliderTileInfo freeform(const QString &guid, qreal nx, qreal ny)
{
    SliderTileInfo t; t.guid = guid; t.hasFreeform = true; t.normX = nx; t.normY = ny; return t;
}

// flatten a model's rows into "a,b|c|d" (rows joined by '|')
static QString dump(const SliderLayoutModel &m)
{
    QStringList rows;
    for (const auto &row : m.rows()) rows << QStringList(row.begin(), row.end()).join(",");
    return rows.join("|");
}

int main()
{
    // --- round-robin seeding: rows-mode ordering fills rows 0,1,2,0,1,2,...
    {
        SliderLayoutModel m;
        m.build({ plain("a"), plain("b"), plain("c"), plain("d"), plain("e") }, 3,
                SliderLayoutModel::Seed::RowsOrder);
        CHECK(m.rowCount() == 3, "seed: row count honored");
        CHECK(dump(m) == "a,d|b,e|c", "seed: round-robin fill in input order");
        CHECK(m.tileCount() == 5, "seed: every tile assigned");
    }

    // --- freeform y-bands map to rows, ordered by x within the band
    {
        SliderLayoutModel m;
        m.build({ freeform("top2", 0.9, 0.1), freeform("bottom", 0.5, 0.95),
                  freeform("top1", 0.2, 0.05), freeform("mid", 0.5, 0.5) }, 3,
                SliderLayoutModel::Seed::FreeformBands);
        CHECK(dump(m) == "top1,top2|mid|bottom", "freeform: y-band -> row, x orders the band");
    }

    // --- freeform seeding: tiles without a freeform position fall back to round-robin
    {
        SliderLayoutModel m;
        m.build({ freeform("f", 0.5, 0.5), plain("p1"), plain("p2") }, 2,
                SliderLayoutModel::Seed::FreeformBands);
        CHECK(dump(m) == "p1|f,p2", "freeform: unplaced tiles round-robin after banded ones");
    }

    // --- stored assignments win and round-trip losslessly through a rebuild
    {
        SliderLayoutModel m;
        m.build({ assigned("x", 1, 0), plain("new"), assigned("y", 1, 1), assigned("z", 0, 0) }, 2,
                SliderLayoutModel::Seed::RowsOrder);
        CHECK(dump(m) == "z,new|x,y", "stored assignments kept; new tile round-robins in");

        // feed the model's own answer back, shuffled, as the stored state
        QVector<SliderTileInfo> stored;
        for (const QString &g : { "y", "new", "z", "x" }) {
            auto p = m.posOf(g);
            stored.push_back(assigned(g, p.row, p.index));
        }
        SliderLayoutModel m2;
        m2.build(stored, 2, SliderLayoutModel::Seed::RowsOrder);
        CHECK(dump(m2) == dump(m), "assignment round-trip is lossless (input order irrelevant)");
    }

    // --- stored rows beyond the configured count clamp into range (setting shrank)
    {
        SliderLayoutModel m;
        m.build({ assigned("far", 9, 0), assigned("near", 0, 0) }, 2,
                SliderLayoutModel::Seed::RowsOrder);
        auto p = m.posOf("far");
        CHECK(p.row == 1, "stored row beyond N clamps to the last row");
        CHECK(m.posOf("missing").row == -1, "unknown guid reports an invalid pos");
    }

    // --- duplicate stored indices stay deterministic (input order breaks the tie)
    {
        SliderLayoutModel m;
        m.build({ assigned("first", 0, 3), assigned("second", 0, 3) }, 2,
                SliderLayoutModel::Seed::RowsOrder);
        CHECK(dump(m) == "first,second|", "duplicate stored indices keep input order");
    }

    // --- moveTile: insert-at-drop semantics
    {
        SliderLayoutModel m;
        m.build({ plain("a"), plain("b"), plain("c"), plain("d") }, 2,
                SliderLayoutModel::Seed::RowsOrder);        // a,c | b,d
        CHECK(m.moveTile("b", 0, 1), "moveTile succeeds");
        CHECK(dump(m) == "a,b,c|d", "cross-row move inserts at the index and shifts the rest");

        m.moveTile("a", 0, 2);                              // within-row: a after c
        CHECK(dump(m) == "b,c,a|d", "same-row move reorders (index counted after removal)");

        m.moveTile("d", 0, -1);
        CHECK(dump(m) == "b,c,a,d|", "index -1 appends at the end of the row");

        m.moveTile("b", 7, 0);
        CHECK(dump(m) == "c,a,d|b", "target row clamps into range");

        m.moveTile("ghost", 1, 0);
        CHECK(dump(m) == "c,a,d|ghost,b", "unknown guid is inserted (tile arriving mid-session)");

        m.removeTile("ghost");
        CHECK(dump(m) == "c,a,d|b" && m.tileCount() == 4, "removeTile drops exactly one tile");
    }

    // --- indices reported by posOf are the implicit list positions after a move
    {
        SliderLayoutModel m;
        m.build({ plain("a"), plain("b"), plain("c") }, 1, SliderLayoutModel::Seed::RowsOrder);
        m.moveTile("c", 0, 0);
        CHECK(m.posOf("c").index == 0 && m.posOf("a").index == 1 && m.posOf("b").index == 2,
              "posOf reindexes the whole row after an insert");
    }

    // --- row offsets: session-only, survive a same-size rebuild, clamp-free storage
    {
        SliderLayoutModel m;
        m.build({ plain("a") }, 3, SliderLayoutModel::Seed::RowsOrder);
        m.setRowOffset(1, -240.0);
        CHECK(m.rowOffset(1) == -240.0, "row offset stored");
        CHECK(m.rowOffset(7) == 0.0, "out-of-range offset reads 0");
        m.build({ plain("a"), plain("b") }, 3, SliderLayoutModel::Seed::RowsOrder);
        CHECK(m.rowOffset(1) == -240.0, "offsets survive a same-row-count rebuild");
        m.build({ plain("a") }, 4, SliderLayoutModel::Seed::RowsOrder);
        CHECK(m.rowOffset(3) == 0.0, "new rows start at offset 0");
    }

    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("all slider layout model checks passed\n");
    return 0;
}
