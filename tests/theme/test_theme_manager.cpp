// theme.manager — unit test of the app theme selection (THEME_AUDIT.md):
// the appearance/theme setting round-trip and normalization, and the
// StyleSheet kill switch that neutralizes every classic getter when the
// Qlementine QStyle owns rendering. Runs offscreen; no style is applied.

#include <QApplication>
#include <QMenu>
#include <QStyle>
#include <cstdio>

#include "ui/style/thememanager.h"
#include "ui/style/stylesheet.h"
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

    // ---- Qlementine leaves QMenu alone (menu-click regression, JOURNAL 2026-08-31) ----
    // Upstream QlementineStyle::polish(QMenu) sets WA_TranslucentBackground and
    // installs a MenuEventFilter that swallows real mouse releases and replays
    // synthetic ones. Qt re-polishes widgets freely (ancestor setStyleSheet,
    // style inheritance churn), stacking filters that swallow each other's
    // synthetic release — clicking any menu item then did nothing, and under
    // QStyleSheetStyle interposition the translucent menus rendered with no
    // background. JahQlementineStyle skips the whole menu treatment; these
    // attributes must stay untouched no matter how often polish runs.
    settings->settings->remove(ThemeManager::settingsKey());
    ThemeManager::applyAtStartup(app);
    QMenu menu;
    QApplication::style()->polish(&menu);
    QApplication::style()->polish(&menu); // simulate a re-polish
    CHECK(!menu.testAttribute(Qt::WA_TranslucentBackground),
          "qlementine: QMenu not made translucent");
    CHECK(!menu.testAttribute(Qt::WA_NoSystemBackground),
          "qlementine: QMenu keeps its system background");

    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
