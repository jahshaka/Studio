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
//
// 3. An icon-over-caption item (an IconMode list: the Materials presets, the
// node palette) is laid out icon ABOVE text. Qlementine ignores
// decorationPosition, sizing and painting every item icon-left/text-right, so
// those lists lost their captions; JahQlementineStyle routes such items to
// QCommonStyle's layout.
//
// 4. An item view whose ACTIVATION IS EXPENSIVE (ThemeRoles::
// setActivateOnDoubleClick — the Avatar module's library, which LOADS a
// character) activates on a double-click or Enter and NEVER on a single click,
// which is what makes its context menu reachable: Qlementine answers
// SH_ItemView_ActivateItemOnSingleClick true and QAbstractItemView then emits
// `activated` on the release of ANY button — including the right button. That
// is the owner's report (2026-09-13): "I right click and it starts reloading
// her... so I can't access the menu".
#include <QApplication>
#include <QStyleOptionTab>
#include <QListWidget>
#include <QStyleOptionViewItem>
#include <QTabBar>
#include <QTest>
#include <QTreeWidget>
#include <QPointer>
#include <QStyle>
#include <cstdio>

#include <oclero/qlementine/style/QlementineStyle.hpp>

#include "ui/controls/hfloatsliderwidget.h"
#include "ui/style/stylesheet.h"
#include "ui/style/themeroles.h"
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
        // (Qt 6.10 paints a DRAGGED tab with position Moving; the correction passes it
        // through — a one-line guard, not observable through this two-tab fixture.)
        QStyleOptionTab dragged = first;
        dragged.position = QStyleOptionTab::OnlyOneTab;
        dragged.selectedPosition = QStyleOptionTab::NotAdjacent;   // older Qt's drag pixmap
        CHECK(s->sizeFromContents(QStyle::CT_TabBarTab, &dragged, QSize(), &bar)
                  != s->sizeFromContents(QStyle::CT_TabBarTab, &first, QSize(), &bar),
              "tabs: an older-Qt lone-tab drag pixmap keeps its lone-tab metrics");
    }

    // ---- 3. icon-over-caption items --------------------------------------------
    {
        QListWidget list;
        list.setViewMode(QListView::IconMode);
        list.setIconSize(QSize(48, 48));
        QPixmap px(48, 48);
        px.fill(Qt::gray);
        auto *item = new QListWidgetItem(QIcon(px), QStringLiteral("Local Normal"), &list);
        list.show();
        QApplication::processEvents();
        QStyleOptionViewItem opt;
        opt.initFrom(&list);
        opt.features = QStyleOptionViewItem::HasDecoration | QStyleOptionViewItem::HasDisplay;
        opt.icon = item->icon();
        opt.text = item->text();
        opt.decorationSize = QSize(48, 48);
        opt.decorationPosition = QStyleOptionViewItem::Top;
        opt.displayAlignment = Qt::AlignHCenter | Qt::AlignBottom;
        const QSize sz = QApplication::style()->sizeFromContents(QStyle::CT_ItemViewItem, &opt, QSize(), &list);
        CHECK(sz.height() >= 48 + opt.fontMetrics.height(),
              "items: an icon-over-caption item is tall enough for the icon AND its caption");
        CHECK(sz.width() < 48 + opt.fontMetrics.horizontalAdvance(opt.text),
              "items: ...and not as wide as icon-beside-text");
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

    // ---- 4. expensive activation takes a double-click ------------------------
    {
        QStyle *s = QApplication::style();
        QTreeWidget plain;
        plain.setColumnCount(1);
        new QTreeWidgetItem(&plain, { QStringLiteral("Jennifer") });
        CHECK(s->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, nullptr, &plain) != 0,
              "activation: an ordinary view keeps Qlementine's single-click activation");

        QTreeWidget library;
        library.setColumnCount(1);
        auto *row = new QTreeWidgetItem(&library, { QStringLiteral("Jennifer") });
        ThemeRoles::setActivateOnDoubleClick(&library);
        CHECK(s->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, nullptr, &library) == 0,
              "activation: a marked view activates on a DOUBLE click instead");
        // ... and the child of a marked view is covered too (a view's viewport
        // is what the style is asked about for some hints).
        CHECK(s->styleHint(QStyle::SH_ItemView_ActivateItemOnSingleClick, nullptr,
                           library.viewport()) == 0,
              "activation: the marked view's viewport answers the same");

        library.show();
        QApplication::processEvents();
        int activations = 0;
        QObject::connect(&library, &QTreeWidget::itemActivated,
                         [&activations](QTreeWidgetItem *, int) { ++activations; });
        const QPoint at = library.visualItemRect(row).center();

        // THE OWNER'S GESTURE: a right-click over a loaded avatar.
        QTest::mouseClick(library.viewport(), Qt::RightButton, Qt::NoModifier, at);
        QApplication::processEvents();
        CHECK(activations == 0, "activation: a RIGHT click activates nothing (the menu's gesture)");

        QTest::mouseClick(library.viewport(), Qt::LeftButton, Qt::NoModifier, at);
        QApplication::processEvents();
        CHECK(activations == 0, "activation: a single LEFT click only selects");
        CHECK(library.currentItem() == row, "activation: ... and it DOES select");

        QTest::mouseDClick(library.viewport(), Qt::LeftButton, Qt::NoModifier, at);
        QApplication::processEvents();
        CHECK(activations == 1, "activation: a DOUBLE click activates (loads) exactly once");

        QTest::keyClick(&library, Qt::Key_Return);
        QApplication::processEvents();
        CHECK(activations == 2, "activation: Enter still activates");

        // The unmarked view is the fail-before: the same right-click on it
        // activates, which is exactly what the Avatar page's library did.
        int plainActivations = 0;
        QObject::connect(&plain, &QTreeWidget::itemActivated,
                         [&plainActivations](QTreeWidgetItem *, int) { ++plainActivations; });
        plain.show();
        QApplication::processEvents();
        QTest::mouseClick(plain.viewport(), Qt::RightButton, Qt::NoModifier,
                          plain.visualItemRect(plain.topLevelItem(0)).center());
        QApplication::processEvents();
        CHECK(plainActivations == 1,
              "activation: an UNMARKED view really does activate on a right click "
              "(the defect this marking fixes)");
    }

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
