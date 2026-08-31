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

    std::printf(failures ? "FAILED (%d)\n" : "PASSED\n", failures);
    return failures ? 1 : 0;
}
