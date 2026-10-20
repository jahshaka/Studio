// ui.streaming_indicator — THE TOOLBAR'S STREAM-IN INDICATOR IS READABLE AND SAYS WHAT IS OWED (BUGS-7 stage 3).
// The owner, 2026-10-20: "why I don't see any loading data while the project streams in any more". The toolbar's
// "Compiling shaders (N)" was there, painted in the toolbar's ButtonText — Qlementine's dark text for a light button
// plate — on the toolbar's dark band: ~1.1:1, measured on the base's Bistro open (spikes/bugs-7/stage0). It also only
// ever counted shaders, while an open owes textures and the first lighting arm too.
//
// THE LAWS (the real theme, offscreen; the label is painted by the real QStyle and photographed with QWidget::grab):
//  * nothing owed: the indicator's action is hidden; shaders, textures and the lighting arm owed, alone or together:
//    shown, its text and tooltip saying each; nothing owed again: hidden;
//  * the painted text's contrast on the toolbar's band >= 4.5:1 (WCAG AA for body text — the photograph's most
//    contrasting glyph pixel against the band's colour). A plain QLabel in the same toolbar (the base's indicator) is
//    photographed beside it and its contrast printed: the negative control.
#include "shell/streamingindicator.h"
#include "ui/style/thememanager.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QLabel>
#include <QTemporaryDir>
#include <QToolBar>

#include <algorithm>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } \
    else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; } std::fflush(stdout); } while (0)

static double luminance(QRgb c) {
    const auto ch = [](int v) { const double s = v / 255.0; return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(qRed(c)) + 0.7152 * ch(qGreen(c)) + 0.0722 * ch(qBlue(c));
}
static double contrast(QRgb a, QRgb b) {
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/// The most contrasting pixel of `label`'s rectangle in the toolbar's photograph against the band (the toolbar's pixel
/// just left of the label, where nothing is drawn).
static double paintedContrast(QToolBar *bar, QLabel *label, QRgb *bandOut, QRgb *textOut) {
    const QImage shot = bar->grab().toImage();
    const qreal dpr = shot.devicePixelRatio();
    const QRect r(label->geometry().topLeft() * dpr, label->geometry().size() * dpr);
    const QRgb band = shot.pixel(std::max(0, r.left() - 2), r.center().y());
    double best = 1.0;
    QRgb text = band;
    for (int y = r.top(); y <= r.bottom() && y < shot.height(); ++y)
        for (int x = r.left(); x <= r.right() && x < shot.width(); ++x) {
            const double c = contrast(shot.pixel(x, y), band);
            if (c > best) { best = c; text = shot.pixel(x, y); }
        }
    if (bandOut) *bandOut = band;
    if (textOut) *textOut = text;
    return best;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir root;
    qputenv("JAHSHAKA_DATA_ROOT", root.path().toUtf8());   // the settings the theme reads: the default theme
    QApplication app(argc, argv);
    ThemeManager::applyAtStartup(app);
    std::printf("== ui.streaming_indicator: the stream-in indicator says what is owed, readably (theme %s)\n",
                qPrintable(ThemeManager::currentThemeId()));

    QToolBar bar;
    StreamingIndicator::Work owed;
    StreamingIndicator indicator(&bar, [&owed]() { return owed; });
    QLabel *plain = new QLabel(QStringLiteral("Compiling shaders (3)"));   // the base's indicator: no role of its own
    bar.addWidget(plain);
    bar.resize(900, 40);
    bar.show();
    app.processEvents();

    struct Case { const char *what; StreamingIndicator::Work w; const char *text; };
    const Case cases[] = {
        { "nothing owed", { 0, 0, 0 }, nullptr },
        { "shaders", { 3, 0, 0 }, "Compiling shaders (3)" },
        { "textures", { 0, 2, 0 }, "Loading textures (2)" },
        { "the lighting arm", { 0, 0, 1 }, "Lighting the world" },
        { "all three", { 3, 2, 1 }, "Compiling shaders (3)  ·  Loading textures (2)  ·  Lighting the world" },
        { "nothing owed again", { 0, 0, 0 }, nullptr },
    };
    for (const Case &c : cases) {
        owed = c.w;
        indicator.refresh();
        app.processEvents();
        const bool shown = indicator.action()->isVisible();
        if (!c.text) {
            CHECK(!shown, "%s: the indicator is hidden", c.what);
        } else {
            const QString want = QString::fromUtf8(c.text);
            CHECK(shown && indicator.label()->text() == want && indicator.action()->toolTip() == want,
                  "%s: shown, saying \"%s\" (\"%s\")", c.what, c.text, qPrintable(indicator.label()->text()));
        }
    }

    owed = { 3, 0, 0 };
    indicator.refresh();
    app.processEvents();
    bar.adjustSize();
    bar.resize(900, std::max(40, bar.height()));
    app.processEvents();
    QRgb band = 0, text = 0, plainBand = 0, plainText = 0;
    const double c = paintedContrast(&bar, indicator.label(), &band, &text);
    const double base = paintedContrast(&bar, plain, &plainBand, &plainText);
    std::printf("    the indicator: text #%06x on the band #%06x, %.2f:1; a plain label (the base): #%06x, %.2f:1\n",
                unsigned(text & 0xffffff), unsigned(band & 0xffffff), c, unsigned(plainText & 0xffffff), base);
    CHECK(c >= 4.5, "the indicator's painted text reads at %.2f:1 on the toolbar's band (bar 4.5:1)", c);

    std::printf(failures ? "ui.streaming_indicator: FAILED\n" : "ui.streaming_indicator: PASS\n");
    return failures ? 1 : 0;
}
