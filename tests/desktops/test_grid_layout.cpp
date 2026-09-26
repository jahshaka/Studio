// desktops.grid_layout — DESKTOP-1 (owner smoke C, 2026-09-26): "the Desktop
// opens EMPTY, or with tiles overlapping in grid view; toggling the layout mode
// restores every tile."
//
// The audit (spikes/desktop-audit-1) named the mechanism: at boot the window is
// shown BEFORE the grid is populated, so every tile is added to a VISIBLE
// parent — and Qt shows such a child later, through a queued call. The old grid
// sized its canvas (`adjustSize`) right after each add, while the tile was
// still hidden, so the canvas stayed 32x32 and nothing re-laid it out when the
// tiles appeared. This suite builds the REAL DynamicGrid that way — shown
// first, tiles added after — and lets the event loop settle (turns counted,
// never wall-clock) BEFORE any user action, then asserts every tile has a
// real, non-overlapping geometry inside the viewport:
//
//   1. Rows, 12 tiles added to a visible grid;
//   2. Rows after a resize (fewer columns) and a newcomer inserted at the head
//      (the inferred overlap: the canvas sized for N-1 tiles);
//   3. Freeform's first entry with never-placed tiles (the pile), and after a
//      resize;
//   4. back to Rows.
//
// Widgets only: no document, no database, no display (offscreen QPA).

#include <QApplication>
#include <QBuffer>
#include <QPixmap>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QTemporaryDir>
#include <cstdio>

#include "data/project.h"
#include "data/settingsmanager.h"
#include "ui/controls/dynamicgrid.h"
#include "ui/controls/itemgridwidget.h"

// THE OWNER the grid connects its tiles to by slot NAME (ProjectManager in the
// app): the slots exist so the connections are real; nothing here clicks.
class Owner : public QWidget
{
    Q_OBJECT
public slots:
    void openProjectFromWidget(ItemGridWidget *, bool) {}
    void closeProjectFromWidget(ItemGridWidget *) {}
    void deleteProjectFromWidget(ItemGridWidget *) {}
    void exportProjectFromWidget(ItemGridWidget *) {}
    void renameProjectFromWidget(ItemGridWidget *) {}
    void moveProjectToDesktop(ItemGridWidget *, int) {}
};

static int failures = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { std::printf("ok: %s\n", qPrintable(QString(name))); }  \
        else { std::printf("FAIL: %s\n", qPrintable(QString(name))); ++failures; } \
    } while (0)

// THE LOOP SETTLES: turns of the event loop, counted — each turn delivers the
// posted events (the queued show, LayoutRequest, the scroll area's resize) the
// previous one produced. 20 turns is far above the chain's depth (3-4).
static void settle()
{
    for (int i = 0; i < 20; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
}

// A real PNG thumbnail, so a tile lays out at the size a desktop tile has
// (the suite links no .qrc; an empty thumbnail makes a caption-only tile).
static QByteArray thumbnail()
{
    static QByteArray bytes;
    if (bytes.isEmpty()) {
        QPixmap pm(460, 215);
        pm.fill(QColor(40, 90, 160));
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        pm.save(&buf, "PNG");
    }
    return bytes;
}

static ProjectTileData tile(int i)
{
    ProjectTileData t;
    t.name = QStringLiteral("Project %1").arg(i);
    t.guid = QStringLiteral("guid-%1").arg(i, 3, 10, QLatin1Char('0'));
    t.thumbnail = thumbnail();
    return t;
}

// Every tile: shown, a real size, inside the viewport horizontally (no
// clipped column) and — when `fitsVertically` — vertically too, and no two
// tiles overlapping. Rects are in the VIEWPORT's coordinates.
static void checkTiles(DynamicGrid &grid, const QString &when, bool fitsVertically,
                       bool disjoint = true)
{
    QWidget *vp = grid.viewport();
    const QRect view(QPoint(0, 0), vp->size());
    QVector<QRect> rects;
    bool allShown = true, allSized = true, allInside = true;
    for (ItemGridWidget *w : std::as_const(grid.originalItems)) {
        if (!w->isVisible()) allShown = false;
        const QRect r(w->mapTo(vp, QPoint(0, 0)), w->size());
        if (r.width() < grid.tileSize.width() / 2 || r.height() < grid.tileSize.height() / 2)
        { allSized = false; std::printf("   %s: %s is %dx%d (tile size %dx%d)\n", qPrintable(when),
                                        qPrintable(w->tileData.name), r.width(), r.height(),
                                        grid.tileSize.width(), grid.tileSize.height()); }
        const bool inX = r.left() >= 0 && r.right() <= view.right();
        const bool inY = !fitsVertically || (r.top() >= 0 && r.bottom() <= view.bottom());
        if (!inX || !inY) {
            allInside = false;
            std::printf("   %s: %s at %d,%d %dx%d outside the %dx%d viewport\n", qPrintable(when),
                        qPrintable(w->tileData.name), r.x(), r.y(), r.width(), r.height(),
                        view.width(), view.height());
        }
        rects.append(r);
    }
    int overlaps = 0;
    for (int i = 0; i < rects.size(); ++i)
        for (int j = i + 1; j < rects.size(); ++j)
            if (rects[i].intersects(rects[j])) ++overlaps;
    CHECK(grid.originalItems.size() > 0, when + ": the grid holds tiles");
    CHECK(allShown, when + ": every tile is shown");
    CHECK(allSized, when + ": every tile has a real size (not a clipped speck)");
    CHECK(allInside, when + ": every tile is inside the viewport");
    if (disjoint)
        CHECK(overlaps == 0, when + QStringLiteral(": no two tiles overlap (%1 overlapping pairs)").arg(overlaps));
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTemporaryDir home;
    SettingsManager::getDefaultManager();   // the grid reads the tile size from it

    Owner host;                              // the ProjectManager's place
    auto *layout = new QHBoxLayout(&host);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *grid = new DynamicGrid(&host);
    layout->addWidget(grid);
    host.resize(1672, 1000);

    // ---- 1. THE BOOT ORDER: shown first, populated after -------------------
    host.show();
    settle();
    for (int i = 0; i < 12; ++i) grid->addToGridView(tile(i), false);
    settle();
    checkTiles(*grid, QStringLiteral("rows, populated while shown"), true);

    // ---- 2. a resize, then a newcomer at the head --------------------------
    host.resize(1000, 1000);
    settle();
    checkTiles(*grid, QStringLiteral("rows, after a resize to 1000"), false);
    grid->insertTileAtHead(tile(12), false);
    settle();
    checkTiles(*grid, QStringLiteral("rows, a newcomer inserted at the head"), false);
    CHECK(grid->originalItems.first()->tileData.guid == tile(12).guid,
          "the newcomer is first in the order");
    const QPoint first = grid->originalItems.first()->mapTo(grid->viewport(), QPoint(0, 0));
    const QPoint second = grid->originalItems.at(1)->mapTo(grid->viewport(), QPoint(0, 0));
    CHECK(first.y() == second.y() && first.x() < second.x(),
          "...and it is laid out first: top-left, the old first tile beside it");
    host.resize(1400, 900);
    settle();

    // ---- 3. Freeform's first entry: grid slots, not a pile -----------------
    grid->setLayoutMode(DynamicGrid::LayoutMode::Freeform);
    settle();
    checkTiles(*grid, QStringLiteral("freeform, first entry"), true);
    // A freeform position is a FRACTION of the canvas (DESKTOPS_SPEC), so a
    // canvas that GROWS spreads the tiles apart and keeps them disjoint...
    host.resize(1672, 1000);
    settle();
    checkTiles(*grid, QStringLiteral("freeform, after a resize that grows"), true);
    // ...and one that SHRINKS moves them together: they stay shown and inside
    // the viewport, but tiles packed at 12 px apart may touch — that is the
    // proportional placement the mode is, not a layout that forgot to run.
    host.resize(1100, 800);
    settle();
    checkTiles(*grid, QStringLiteral("freeform, after a resize that shrinks"), true, false);

    // ---- 4. back to Rows -----------------------------------------------------
    grid->setLayoutMode(DynamicGrid::LayoutMode::Rows);
    settle();
    checkTiles(*grid, QStringLiteral("rows, back from freeform"), false);

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall grid-layout assertions passed\n", failures);
    return failures ? 1 : 0;
}

#include "test_grid_layout.moc"
