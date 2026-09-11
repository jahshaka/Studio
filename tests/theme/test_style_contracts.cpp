// theme.style_contracts — two contracts between our widgets and the Qlementine
// style that the theme sweep (lane 16) surfaced once the Classic sheets stopped
// hiding them:
//
// 1. A widget's QProxyStyle must never OWN the application's style.
//
// QProxyStyle(QStyle *base) takes ownership of `base`. HFloatSliderWidget (every
// slider row in the property panels) and ColorView built their proxies from
// this->style(). While each row carried the Classic .ui sheet, that was the
// row's own QStyleSheetStyle; once the sweep removed the sheet under Qlementine
// it became THE APPLICATION'S QlementineStyle — so the first slider row a panel
// rebuild destroyed deleted the app style out from under the whole UI, and the
// app died at shutdown inside qlementine::WidgetAnimationManager::removeWidget
// (a freed style's animator map). This suite builds and destroys rows under a
// live Qlementine style and asserts the style survives, then does the same
// under Classic (whose rows keep their sheet and their old proxy base).
//
// 2. A tab's size, text rect and painting agree even when QTabBar hands the
// style a STALE first/last position (its cached visible indices lag a layout
// that runs as a hidden bar is shown again). Qlementine insets first/last
// tabs, so a stale position sized the editor's "Tray" dock tab as a middle tab
// and then elided it to "T…" at paint. JahQlementineStyle (thememanager.cpp)
// recomputes the position from the bar; this checks it with a forged stale
// option.
#include <QApplication>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QPointer>
#include <QStyle>
#include <cstdio>

#include <oclero/qlementine/style/QlementineStyle.hpp>

#include "ui/controls/hfloatsliderwidget.h"
#include "ui/style/stylesheet.h"
#include "ui/style/thememanager.h"

namespace {
struct Bar : QTabBar
{
    using QTabBar::initStyleOption;
};
} // namespace

static int failures = 0;
#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) { std::printf("ok: %s\n", name); }                       \
        else { std::printf("FAIL: %s\n", name); ++failures; }              \
    } while (0)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // The app's own startup path: a private JAHSHAKA_DATA_ROOT (CMake) holds no
    // appearance/theme key, so this installs the default — Qlementine, through
    // JahQlementineStyle.
    ThemeManager::applyAtStartup(app);
    CHECK(!ThemeManager::classicActive(), "startup: the default theme is Qlementine");
    QPointer<QStyle> appStyle = QApplication::style();
    CHECK(qobject_cast<oclero::qlementine::QlementineStyle *>(appStyle.data()) != nullptr,
          "startup: the app style is a QlementineStyle");

    // ---- 2. stale tab positions ---------------------------------------------
    {
        Bar bar;
        bar.addTab(QStringLiteral("Timeline"));
        bar.addTab(QStringLiteral("Tray"));
        bar.setCurrentIndex(1);
        bar.show();
        QApplication::processEvents();
        QStyleOptionTab right;
        bar.initStyleOption(&right, 1);
        CHECK(right.tabIndex == 1 && right.position == QStyleOptionTab::End,
              "tabs: the last tab's option says End");
        QStyleOptionTab stale = right;
        stale.position = QStyleOptionTab::Middle;   // what the stale layout pass saw
        QStyle *s = QApplication::style();
        CHECK(s->sizeFromContents(QStyle::CT_TabBarTab, &stale, QSize(), &bar)
                  == s->sizeFromContents(QStyle::CT_TabBarTab, &right, QSize(), &bar),
              "tabs: a stale position does not change the tab's size");
        CHECK(s->subElementRect(QStyle::SE_TabBarTabText, &stale, &bar)
                  == s->subElementRect(QStyle::SE_TabBarTabText, &right, &bar),
              "tabs: a stale position does not change the tab's text rect");
        QStyleOptionTab lone;
        bar.initStyleOption(&lone, 0);
        lone.position = QStyleOptionTab::OnlyOneTab;   // Timeline, as the stale pass saw it
        QStyleOptionTab first;
        bar.initStyleOption(&first, 0);
        CHECK(s->sizeFromContents(QStyle::CT_TabBarTab, &lone, QSize(), &bar)
                  == s->sizeFromContents(QStyle::CT_TabBarTab, &first, QSize(), &bar),
              "tabs: a stale lone-tab position does not widen the first tab");
        QStyleOptionTab dragged = first;
        dragged.position = QStyleOptionTab::OnlyOneTab;
        dragged.selectedPosition = QStyleOptionTab::NotAdjacent;   // a drag pixmap
        CHECK(s->sizeFromContents(QStyle::CT_TabBarTab, &dragged, QSize(), &bar)
                  != s->sizeFromContents(QStyle::CT_TabBarTab, &first, QSize(), &bar),
              "tabs: a dragged tab's pixmap keeps its lone-tab metrics");
    }

    // ---- 1. proxy ownership ---------------------------------------------------

    // ---- Qlementine: rows carry no sheet, so this->style() is the app style
    StyleSheet::setClassicThemeActive(false);
    {
        auto *row = new HFloatSliderWidget;
        CHECK(row->styleSheet().isEmpty(), "qlementine: a slider row carries no sheet");
        row->show();
        QApplication::processEvents();
        delete row;
    }
    QApplication::processEvents();
    CHECK(!appStyle.isNull(), "qlementine: destroying a slider row leaves the app style alive");
    CHECK(QApplication::style() == appStyle.data(), "qlementine: the app style is still the one installed");
    {
        // several rows, destroyed in turn (a panel rebuild)
        QWidget panel;
        for (int i = 0; i < 8; ++i) new HFloatSliderWidget(&panel);
        panel.show();
        QApplication::processEvents();
    }
    QApplication::processEvents();
    CHECK(!appStyle.isNull(), "qlementine: a rebuilt panel of rows leaves the app style alive");

    // ---- Classic: the row keeps its archived sheet (and its old proxy base)
    StyleSheet::setClassicThemeActive(true);
    {
        HFloatSliderWidget row;
        CHECK(!row.styleSheet().isEmpty(), "classic: a slider row keeps its archived sheet");
        row.show();
        QApplication::processEvents();
    }
    QApplication::processEvents();
    CHECK(!appStyle.isNull(), "classic: destroying a slider row leaves the app style alive");

    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
