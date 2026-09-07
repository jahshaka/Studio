// theme.manager — unit test of the app theme selection (THEME_AUDIT.md):
// the appearance/theme setting round-trip and normalization, and the
// StyleSheet kill switch that neutralizes every classic getter when the
// Qlementine QStyle owns rendering. Runs offscreen; no style is applied.

#include <QApplication>
#include <QFont>
#include <QMenu>
#include <QPushButton>
#include <QStyle>
#include <cstdio>

#include "ui/style/thememanager.h"
#include "ui/style/stylesheet.h"
#include "data/constants.h"
#include "data/settingsmanager.h"

static int failures = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { std::printf("ok: %s\n", name); }                       \
        else { std::printf("FAIL: %s\n", name); ++failures; }              \
    } while (0)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // ---- setting round-trip + normalization ----
    auto *settings = SettingsManager::getDefaultManager();
    settings->settings->remove(ThemeManager::settingsKey());

    CHECK(ThemeManager::currentThemeId() == ThemeManager::qlementineDarkId(),
          "no key -> default is qlementine-dark");

    ThemeManager::setThemeId(ThemeManager::classicId());
    CHECK(ThemeManager::currentThemeId() == ThemeManager::classicId(),
          "classic persists and reads back");

    ThemeManager::setThemeId(ThemeManager::qlementineDarkId());
    CHECK(ThemeManager::currentThemeId() == ThemeManager::qlementineDarkId(),
          "qlementine-dark persists and reads back");

    ThemeManager::setThemeId("no-such-theme");
    CHECK(ThemeManager::currentThemeId() == ThemeManager::qlementineDarkId(),
          "unknown id normalizes to the default");

    settings->settings->remove(ThemeManager::settingsKey());

    // ---- StyleSheet kill switch ----
    StyleSheet::setClassicThemeActive(true);
    CHECK(!StyleSheet::QPushButtonBlue().isEmpty(), "classic: getters return CSS");
    CHECK(!StyleSheet::QMenuDark().isEmpty(), "classic: menu getter returns CSS");
    CHECK(!StyleSheet::ItemGridTileBorder(2).isEmpty(), "classic: parameterized getter returns CSS");

    StyleSheet::setClassicThemeActive(false);
    CHECK(StyleSheet::QPushButtonBlue().isEmpty(), "qlementine: getters neutralized");
    CHECK(StyleSheet::QMenuDark().isEmpty(), "qlementine: menu getter neutralized");
    CHECK(StyleSheet::ItemGridTileBorder(2).isEmpty(), "qlementine: parameterized getter neutralized");
    CHECK(StyleSheet::PreferencesTabs().isEmpty(), "qlementine: preferences tabs getter neutralized");

    // ---- header glyph buttons (owner report 2026-09-07) ----------------------
    // The three header icons (Publish / Help / Preferences) are icon-font
    // characters on the header bar. Two things went wrong and are pinned here:
    // Qlementine painted its grey button PLATE behind them (read as "a grey
    // background baked into the icon"), and a font pushed with setFont() was
    // dropped by the next repolish, leaving Publish at the inherited UI size
    // beside two 28px siblings. The sheet carries BOTH: no plate, and the font.
    {
        QFont iconFont(QStringLiteral("FontAwesome"));
        iconFont.setPixelSize(28);

        StyleSheet::setClassicThemeActive(false);
        const QString sheet = ThemeManager::headerGlyphButtonSheet(iconFont);
        CHECK(sheet.contains("background: transparent"),
              "glyph button: no button plate under qlementine");
        CHECK(sheet.contains("font-size: 28px"),
              "glyph button: the sheet carries the icon size");
        CHECK(sheet.contains("FontAwesome"),
              "glyph button: the sheet carries the icon family");

        QPushButton publish, help;
        publish.setObjectName(QStringLiteral("publish_menu"));
        help.setObjectName(QStringLiteral("helpButton"));
        ThemeManager::applyHeaderGlyphButton(&publish, iconFont);
        ThemeManager::applyHeaderGlyphButton(&help, iconFont);
        CHECK(publish.styleSheet() == help.styleSheet(),
              "glyph buttons: all three header icons get the SAME sheet");
        // The regression itself: a repolish (any setStyleSheet on the widget,
        // which updateTopMenuStates does to Publish and only to Publish) must
        // not shrink it. Re-applying is idempotent, sheet and all.
        QApplication::style()->polish(&publish);
        ThemeManager::applyHeaderGlyphButton(&publish, iconFont);
        CHECK(publish.styleSheet().contains("font-size: 28px"),
              "glyph button: the size survives a re-apply after a polish");

    }

    // ---- the frame-stats readout is OFF by default ---------------------------
    // Four doors read `show_fps` (F3, the View Options row, Preferences,
    // editor.setOverlays({stats})) and all four pass this constant. A user who
    // switched it on keeps it — the DEFAULT, for a settings file that has
    // never seen the key, is off (owner report 2026-09-07).
    CHECK(Constants::SHOW_FPS_DEFAULT == false,
          "show_fps defaults to OFF");
    {
        auto *s = SettingsManager::getDefaultManager();
        s->settings->remove(QStringLiteral("show_fps"));
        CHECK(s->getValue("show_fps", Constants::SHOW_FPS_DEFAULT).toBool() == false,
              "a settings file with no show_fps key reads back OFF");
        s->setValue("show_fps", true);
        CHECK(s->getValue("show_fps", Constants::SHOW_FPS_DEFAULT).toBool() == true,
              "an explicit choice survives the default");
        s->settings->remove(QStringLiteral("show_fps"));
    }

    // ---- QMenu polish contract (menu-click regression, JOURNAL 2026-08-31) ----
    // Upstream QlementineStyle::polish(QMenu) installs a MenuEventFilter (a
    // plain QObject child of the menu) that swallows real mouse releases and
    // replays synthetic ones. Qt re-polishes widgets freely (ancestor
    // setStyleSheet, style inheritance churn), stacking filters that swallow
    // each other's synthetic release — clicking any menu item then did
    // NOTHING. JahQlementineStyle skips that filter entirely (standard Qt
    // click handling) while keeping the translucent-window bits so a popup is
    // one rounded panel, not a box in an opaque box. Verify both halves, and
    // that a re-polish stays clean.
    settings->settings->remove(ThemeManager::settingsKey());
    ThemeManager::applyAtStartup(app);
    QMenu menu;
    menu.addAction("probe");
    QApplication::style()->polish(&menu);
    QApplication::style()->polish(&menu); // simulate a re-polish
    CHECK(menu.testAttribute(Qt::WA_TranslucentBackground),
          "qlementine: QMenu window translucent (single rounded popup)");
    int plainQObjectChildren = 0;
    for (const QObject *child : menu.children())
        if (qstrcmp(child->metaObject()->className(), "QObject") == 0)
            ++plainQObjectChildren;
    CHECK(plainQObjectChildren == 0,
          "qlementine: no MenuEventFilter children even after a re-polish");

    // ---- the same glyph buttons under Classic --------------------------------
    // LAST, because it flips ThemeManager's own live flag (the getters read
    // that one, not StyleSheet's mirror) and the classic branch of
    // applyAtStartup only swaps the application font. Classic keeps its two
    // archived sheets bit-for-bit; the glyph sheet is neutralized there.
    {
        QFont iconFont(QStringLiteral("FontAwesome"));
        iconFont.setPixelSize(28);
        ThemeManager::setThemeId(ThemeManager::classicId());
        ThemeManager::applyAtStartup(app);
        CHECK(ThemeManager::classicActive(), "classic is live for the checks below");
        CHECK(ThemeManager::headerGlyphButtonSheet(iconFont).isEmpty(),
              "classic: the glyph sheet is neutralized (HelpButton/PrefsButton own it)");
        QPushButton classicPrefs, classicHelp;
        classicPrefs.setObjectName(QStringLiteral("prefsButton"));
        classicHelp.setObjectName(QStringLiteral("helpButton"));
        ThemeManager::applyHeaderGlyphButton(&classicPrefs, iconFont);
        ThemeManager::applyHeaderGlyphButton(&classicHelp, iconFont);
        CHECK(classicPrefs.styleSheet() == StyleSheet::PrefsButton(),
              "classic: the gear keeps PrefsButton() bit-for-bit");
        CHECK(classicHelp.styleSheet() == StyleSheet::HelpButton(),
              "classic: help keeps HelpButton() bit-for-bit");
        CHECK(classicPrefs.font().pixelSize() == 28,
              "classic: the icon font is still applied");
        settings->settings->remove(ThemeManager::settingsKey());
    }

    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
