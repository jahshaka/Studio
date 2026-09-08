// ui.project_tile — THE OPEN PROJECT IS SPOTTABLE ON THE DESKTOP (owner
// request 2026-09-08: "make the open project have a dark blue bar where the
// text is, not black, so we can easily spot it").
//
// The desktop's project tiles (src/ui/controls/itemgridwidget.cpp) all carried
// the same black caption bar, so the open project was marked only by a dashed
// accent border — which the Qlementine theme neutralizes to nothing (every
// classic StyleSheet getter returns "" there). The bar colour is now a theme
// token (ThemeManager::tileCaptionBarColor) and this suite pins it in PIXELS,
// on real tiles, in both themes:
//
//   * two tiles, one marked open: the open one's bar is dark blue, the other's
//     is black, and the blue reads (blue channel dominant, dark, and >= 4.5:1
//     against the white caption text so the name stays legible);
//   * the state is LIVE: setOpenProject(true/false) repaints an existing tile
//     (the desktop also rebuilds its tiles on open/close, but the tile must not
//     depend on that);
//   * CLASSIC IS UNTOUCHED: with the archived theme active both tiles keep the
//     black bar they always had, and the bar's geometry (font size, padding,
//     the two bottom radii that close the tile card) is byte-identical between
//     the two states in both themes — only the colour ever moves.
//
// Widgets only: no document, no database, no display (offscreen QPA).

#include <QApplication>
#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <cstdio>
#include <cmath>

#include "data/project.h"
#include "ui/controls/itemgridwidget.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"

static int failures = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { std::printf("ok: %s\n", name); }                       \
        else { std::printf("FAIL: %s\n", name); ++failures; }              \
    } while (0)

// A real PNG thumbnail so the tile lays out exactly like a desktop tile does
// (the suite links no .qrc, so :/images/preview.png would decode to nothing).
static QByteArray makeThumbnail(const QColor &c, QSize size)
{
    QPixmap pm(size);
    pm.fill(c);
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    pm.save(&buf, "PNG");
    return bytes;
}

// The tile's caption bar is the one direct-child QLabel that is not the image.
static QLabel *captionBar(ItemGridWidget *tile)
{
    for (QLabel *l : tile->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly))
        if (l->objectName() != QLatin1String("image")) return l;
    return nullptr;
}

// The bar's colour AS RENDERED: grab the whole tile (QWidget::grab, the
// composed card — not the label in isolation) and take the DOMINANT colour
// inside the caption bar's geometry. Dominant, not the centre pixel: the
// caption text is centred and antialiased, so a single sample can land on a
// glyph. The band's background is the overwhelming majority of those pixels.
static QColor barColour(ItemGridWidget *tile)
{
    QLabel *bar = captionBar(tile);
    if (!bar) return QColor();
    tile->adjustSize();
    tile->layout()->activate();
    const QImage shot = tile->grab().toImage();
    const QRect r = bar->geometry().adjusted(4, 2, -4, -2).intersected(shot.rect());
    if (r.isEmpty()) return QColor();

    QHash<QRgb, int> histogram;
    for (int y = r.top(); y <= r.bottom(); ++y)
        for (int x = r.left(); x <= r.right(); ++x)
            ++histogram[shot.pixel(x, y)];

    QRgb best = 0; int bestCount = -1;
    for (auto it = histogram.cbegin(); it != histogram.cend(); ++it)
        if (it.value() > bestCount) { best = it.key(); bestCount = it.value(); }
    return QColor(best);
}

static double relativeLuminance(const QColor &c)
{
    auto ch = [](double v) {
        v /= 255.0;
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * ch(c.red()) + 0.7152 * ch(c.green()) + 0.0722 * ch(c.blue());
}

static double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

static ItemGridWidget *makeTile(const QString &name, bool open)
{
    ProjectTileData data;
    data.name = name;
    data.guid = QStringLiteral("guid-") + name;
    data.thumbnail = makeThumbnail(QColor(90, 90, 90), QSize(200, 120));
    auto *tile = new ItemGridWidget(data, QSize(200, 120), QSize(24, 24), nullptr, open);
    tile->adjustSize();
    return tile;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    const QColor black(Qt::black);

    // ---- the Qlementine theme (the default): two tiles, one open ----------
    StyleSheet::setClassicThemeActive(false);
    {
        ItemGridWidget *openTile  = makeTile(QStringLiteral("Open World"), true);
        ItemGridWidget *plainTile = makeTile(QStringLiteral("Other World"), false);

        const QColor openBar  = barColour(openTile);
        const QColor plainBar = barColour(plainTile);

        CHECK(plainBar == black, "an ordinary tile's caption bar is still black");
        CHECK(openBar != black, "the open project's caption bar is not black");
        CHECK(openBar == ThemeManager::tileCaptionBarColor(true),
              "the open bar renders exactly the theme token colour");
        CHECK(openBar.blue() > openBar.red() && openBar.blue() > openBar.green(),
              "the open bar is BLUE (blue channel dominates)");
        CHECK(openBar.value() < 128, "the open bar is DARK blue (value < 50%)");
        CHECK(contrastRatio(openBar, QColor(Qt::white)) >= 4.5,
              "white caption text stays readable on the open bar (>= 4.5:1)");

        // the tile state the readback reports (desktop.tiles()'s `open` field)
        CHECK(openTile->isOpenProject, "the open tile reports isOpenProject");
        CHECK(!plainTile->isOpenProject, "the ordinary tile does not");

        // ---- live: no rebuild needed for a tile to change state -----------
        openTile->setOpenProject(false);
        CHECK(barColour(openTile) == black,
              "closing the project returns the bar to black, live");
        CHECK(!openTile->isOpenProject, "and clears the readback flag");

        plainTile->setOpenProject(true);
        CHECK(barColour(plainTile) == ThemeManager::tileCaptionBarColor(true),
              "opening a project turns its bar dark blue, live");
        CHECK(plainTile->isOpenProject, "and sets the readback flag");

        // removeHighlight() (the tile's Close button path) is the same state
        plainTile->removeHighlight();
        CHECK(barColour(plainTile) == black, "removeHighlight() restores the black bar");

        delete openTile;
        delete plainTile;
    }

    // ---- the archived Classic theme: nothing changed ----------------------
    StyleSheet::setClassicThemeActive(true);
    {
        ItemGridWidget *openTile  = makeTile(QStringLiteral("Open World"), true);
        ItemGridWidget *plainTile = makeTile(QStringLiteral("Other World"), false);

        CHECK(barColour(openTile) == black, "classic: the open tile's bar stays black");
        CHECK(barColour(plainTile) == black, "classic: an ordinary tile's bar stays black");
        CHECK(ThemeManager::tileCaptionBarColor(true) == black,
              "classic: the theme token itself is black");

        delete openTile;
        delete plainTile;
    }

    // ---- the sheet moves the COLOUR and nothing else ----------------------
    {
        StyleSheet::setClassicThemeActive(false);
        const QString openSheet  = ThemeManager::tileCaptionBarSheet(15, 3, true);
        const QString plainSheet = ThemeManager::tileCaptionBarSheet(15, 3, false);
        CHECK(openSheet != plainSheet, "the two sheets differ");

        QString openStripped = openSheet, plainStripped = plainSheet;
        openStripped.remove(ThemeManager::tileCaptionBarColor(true).name());
        plainStripped.remove(ThemeManager::tileCaptionBarColor(false).name());
        CHECK(openStripped == plainStripped,
              "geometry (font, padding, corner radii) is identical in both states");

        // the classic band, unchanged from the sheet the tile always carried
        CHECK(plainSheet.contains(QLatin1String("font-size: 15px"))
                  && plainSheet.contains(QLatin1String("padding-bottom: 2px"))
                  && plainSheet.contains(QLatin1String("border-bottom-left-radius: 3px"))
                  && plainSheet.contains(QLatin1String("border-bottom-right-radius: 3px"))
                  && plainSheet.contains(QLatin1String("color: white")),
              "the caption sheet still carries the tile's shipped geometry");
    }

    StyleSheet::setClassicThemeActive(false);

    if (failures) std::printf("project tile: %d FAILURES\n", failures);
    else          std::printf("project tile: all checks passed\n");
    return failures ? 1 : 0;
}
