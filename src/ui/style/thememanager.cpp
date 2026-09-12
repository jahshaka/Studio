/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/style/thememanager.h"
#include "ui/style/stylesheet.h"
#include "ui/style/themeroles.h"
#include "data/settingsmanager.h"
#include "data/constants.h"

#include <QApplication>
#include <QFontDatabase>

#include <oclero/qlementine/style/QlementineStyle.hpp>

#include <QFileDialog>
#include <QHBoxLayout>
#include <QMenu>
#include <QMetaObject>
#include <QPushButton>
#include <QSet>
#include <QStyleOptionTab>
#include <QStyleOptionViewItem>
#include <QTabBar>
#include <QWidgetAction>

#include <oclero/qlementine/widgets/Switch.hpp>

static bool s_classicActive = false;

namespace {

// Every sheet ThemeManager hands out, remembered by value. The live theme walk
// (app.styleSheets, the theme.sheets suite) asks isThemeSheet() of every
// non-empty widget sheet it finds: a sheet that came from here is the theme's
// own chrome; anything else under Qlementine is a raw sheet that interposes
// QStyleSheetStyle over the style — the regression the walk exists to catch.
QSet<QString> &themeSheetRegistry()
{
    static QSet<QString> sheets;
    return sheets;
}

QString themeSheet(const QString &css)
{
    if (!css.isEmpty()) themeSheetRegistry().insert(css);
    return css;
}

// (The Qt 6.10 + Qlementine combo-popup stack overflow used to be worked around
// here, by deferring the popup item view's polish one event-loop tick. It is
// FIXED AT THE SOURCE since 2026-09-08 — qlementine is vendored, and its
// ComboboxItemViewFilter no longer calls the container-creating
// QComboBox::view() from a ChildAdded handler that Qt emits from the
// container's own constructor. See thirdparty/qlementine/PROVENANCE.md; the
// combo cases in tests/theme guard it. The deferral is gone: the popup is now
// polished inline, exactly as upstream intended.)
class JahQlementineStyle : public oclero::qlementine::QlementineStyle
{
public:
    using oclero::qlementine::QlementineStyle::QlementineStyle;

    void polish(QWidget *w) override
    {
        // Keep Qlementine's hands off QMenu (verified by gdb trace, 2026-08-31):
        // its menu polish sets WA_TranslucentBackground and installs a
        // MenuEventFilter that swallows every real mouse release and replays a
        // synthetic one after a flash animation. Both assumptions break here:
        // (1) polish() is not idempotent but Qt re-polishes widgets freely
        // (Qlementine's own setWindowFlag() inside polish() recurses via
        // inheritStyle(), and every ancestor setStyleSheet() re-polishes all
        // descendants), so menus collect 2..11 stacked filters — and stacked
        // filters swallow each other's synthetic release, so clicking a menu
        // item does NOTHING (the desktop layout/switcher popups). (2) legacy
        // stylesheets up the parent chain (mainwindow.ui / projectmanager.ui
        // root sheets) interpose QStyleSheetStyle, which takes over menu
        // painting and paints no panel background, while the menu window is
        // translucent — a see-through menu (the desktop tile context menu).
        // Menus in this app are painted by QStyleSheetStyle/palette anyway;
        // plain QCommonStyle polish keeps them clickable. We do keep the
        // translucent-window bits (and force PM_MenuPanelWidth to 0 below) so
        // a popup is ONE rounded panel filling its window edge-to-edge —
        // without them the rounded panel paints inside an opaque square
        // window ("box in a box"). Attribute sets are idempotent, so the
        // re-polish churn that stacked filters is harmless here.
        if (auto *menu = qobject_cast<QMenu *>(w)) {
            QCommonStyle::polish(w);
            menu->setBackgroundRole(QPalette::NoRole);
            menu->setAutoFillBackground(false);
            menu->setAttribute(Qt::WA_TranslucentBackground, true);
            menu->setAttribute(Qt::WA_OpaquePaintEvent, false);
            menu->setAttribute(Qt::WA_NoSystemBackground, true);
            return;
        }

        oclero::qlementine::QlementineStyle::polish(w);
    }

    // The base class overloads polish(QApplication*)/polish(QPalette&); keep
    // them reachable despite the QWidget override above.
    using oclero::qlementine::QlementineStyle::polish;

    // Focus-visible semantics, app-wide: Qlementine rings every focused
    // control (buttons included), so plain window activation showed a blue
    // ring on whichever chrome button happened to hold initial focus (top
    // menu, "Import Scene", ...). Qt flags the window with
    // WA_KeyboardFocusChange the first time focus moves via the keyboard —
    // until then the ring is suppressed; Tab users keep full focus
    // visibility.
    void drawControl(ControlElement element, const QStyleOption *opt,
                     QPainter *p, const QWidget *w) const override
    {
        if (element == QStyle::CE_FocusFrame && w && w->window()
            && !w->window()->testAttribute(Qt::WA_KeyboardFocusChange))
            return;
        QStyleOptionTab fixed;
        if (element == QStyle::CE_TabBarTab && correctTabPosition(opt, w, &fixed)) {
            oclero::qlementine::QlementineStyle::drawControl(element, &fixed, p, w);
            return;
        }
        if (element == QStyle::CE_ItemViewItem && isIconOverText(opt)) {
            QCommonStyle::drawControl(element, opt, p, w);
            return;
        }
        oclero::qlementine::QlementineStyle::drawControl(element, opt, p, w);
    }

    // ICON-OVER-CAPTION ITEMS (theme sweep, 2026-09-11). Qlementine lays every
    // item-view item out as icon-left/text-right and sizes it that way —
    // it ignores QStyleOptionViewItem::decorationPosition — so an IconMode list
    // (the Materials presets, the node palette, the Sample Scenes tiles) lost
    // its captions: squeezed beside the icon and elided to "Lo…", or clipped
    // away entirely. Such items get QCommonStyle's layout (which honours Top),
    // while its background/selection still comes from Qlementine through
    // proxy()->drawPrimitive(PE_PanelItemViewItem). The Classic sheets had
    // hidden this: QStyleSheetStyle laid those items out itself.
    static bool isIconOverText(const QStyleOption *opt)
    {
        const auto *item = qstyleoption_cast<const QStyleOptionViewItem *>(opt);
        return item && item->decorationPosition == QStyleOptionViewItem::Top;
    }

    // TAB METRICS FROM THE TAB BAR ITSELF (theme sweep, 2026-09-11). Qlementine
    // insets the first and last tab of a bar by one spacing unit, and reads
    // "first/last" from the option's `position` in three places that must agree
    // — the tab's size (CT_TabBarTab), its text rect (SE_TabBarTabText, which is
    // what QTabBar elides the label against) and its painting. QTabBar fills
    // `position` from cached first/last-visible indices, and those can be stale
    // for the layout pass that runs when a hidden bar is shown again: the
    // editor's Timeline | Tray dock bar, after a trip to the Desktop, sized
    // Timeline as a lone tab and Tray as a middle one, then painted Tray as the
    // LAST tab — its text rect came out 8 px short and "Tray" drew as "T…".
    // (The root sheet mainwindow.ui carried until this sweep hid it: the
    // stylesheet style computed tab sizes itself.) Recomputing the position
    // from the bar's actual visible tabs makes all three agree. A tab being
    // DRAGGED is painted with position Moving (Qt 6.10, measured in the theme
    // review) and is left alone, as is the lone-tab drag pixmap older Qt used
    // (OnlyOneTab + NotAdjacent). [Upstream-fix candidate: tabExtraPadding in the vendored
    // QlementineStyle.cpp could do this itself.]
    static bool correctTabPosition(const QStyleOption *opt, const QWidget *w, QStyleOptionTab *out)
    {
        const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(opt);
        const auto *bar = qobject_cast<const QTabBar *>(w);
        if (!tab || !bar || tab->tabIndex < 0 || tab->tabIndex >= bar->count()) return false;
        if (tab->position == QStyleOptionTab::Moving) return false;
        if (tab->position == QStyleOptionTab::OnlyOneTab
            && tab->selectedPosition == QStyleOptionTab::NotAdjacent)
            return false;
        int first = -1, last = -1;
        for (int i = 0; i < bar->count(); ++i) {
            if (!bar->isTabVisible(i)) continue;
            if (first < 0) first = i;
            last = i;
        }
        if (first < 0) return false;
        const int i = tab->tabIndex;
        const QStyleOptionTab::TabPosition position =
            first == last ? QStyleOptionTab::OnlyOneTab
            : i == first  ? QStyleOptionTab::Beginning
            : i == last   ? QStyleOptionTab::End
                          : QStyleOptionTab::Middle;
        if (position == tab->position) return false;
        *out = *tab;
        out->position = position;
        return true;
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption *opt, const QSize &size,
                           const QWidget *w) const override
    {
        QStyleOptionTab fixed;
        if (type == QStyle::CT_TabBarTab && correctTabPosition(opt, w, &fixed))
            return oclero::qlementine::QlementineStyle::sizeFromContents(type, &fixed, size, w);
        if (type == QStyle::CT_ItemViewItem && isIconOverText(opt))
            return QCommonStyle::sizeFromContents(type, opt, size, w);
        return oclero::qlementine::QlementineStyle::sizeFromContents(type, opt, size, w);
    }

    QRect subElementRect(SubElement element, const QStyleOption *opt,
                         const QWidget *w) const override
    {
        QStyleOptionTab fixed;
        if ((element == QStyle::SE_TabBarTabText || element == QStyle::SE_TabBarTabLeftButton
             || element == QStyle::SE_TabBarTabRightButton)
            && correctTabPosition(opt, w, &fixed))
            return oclero::qlementine::QlementineStyle::subElementRect(element, &fixed, w);
        return oclero::qlementine::QlementineStyle::subElementRect(element, opt, w);
    }

    // Same rule for the OTHER focus-ring path: widgets carrying a stylesheet
    // are painted by QStyleSheetStyle, which draws focus as PE_FrameFocusRect
    // (the classic dashed rectangle) and delegates that primitive here. The
    // chrome-button sheets made footer buttons take this path — window
    // activation then ringed "Import Scene" with a dashed box. Keyboard users
    // keep the ring the moment focus first moves via Tab.
    void drawPrimitive(PrimitiveElement element, const QStyleOption *opt,
                       QPainter *p, const QWidget *w) const override
    {
        if (element == QStyle::PE_FrameFocusRect && w && w->window()
            && !w->window()->testAttribute(Qt::WA_KeyboardFocusChange))
            return;
        oclero::qlementine::QlementineStyle::drawPrimitive(element, opt, p, w);
    }

    // Upstream reserves a drop-shadow band around every menu
    // (PM_MenuPanelWidth) and repositions the window to compensate — from the
    // MenuEventFilter we deliberately do not install. With the band at 0 the
    // rounded panel fills the popup window exactly: correct position, single
    // box, no shadow band. Inner item padding (PM_MenuHMargin/VMargin) stays.
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override
    {
        if (metric == QStyle::PM_MenuPanelWidth)
            return 0;
        return oclero::qlementine::QlementineStyle::pixelMetric(metric, option, widget);
    }

    // Qlementine answers SH_ItemView_ActivateItemOnSingleClick with true,
    // which inside the NON-NATIVE QFileDialog means a single click on a file
    // ACTIVATES it — the dialog accepts and the import starts while the user
    // is still browsing (owner-reported: "the file dialog imports on one
    // click"). Scope the platform-default double-click activation to file
    // dialogs only: selection (and multi-select for batch imports) stays a
    // click, importing takes Open, Enter or a double-click. Everything else
    // keeps Qlementine's behavior.
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override
    {
        if (hint == QStyle::SH_ItemView_ActivateItemOnSingleClick) {
            for (const QWidget *w = widget; w; w = w->parentWidget()) {
                if (qobject_cast<const QFileDialog *>(w))
                    return 0;
                // ... and any view that says its activation is expensive
                // (ThemeRoles::setActivateOnDoubleClick). The Avatar module's
                // library LOADS a character on activation, and Qt emits
                // `activated` on a RIGHT-button release too, so the owner's
                // right-click reloaded the avatar instead of opening the menu.
                if (w->property(ThemeRoles::activateOnDoubleClickProperty()).toBool())
                    return 0;
            }
        }
        return oclero::qlementine::QlementineStyle::styleHint(hint, option, widget, returnData);
    }
};

} // namespace

QString ThemeManager::settingsKey()
{
    return QStringLiteral("appearance/theme");
}

QString ThemeManager::currentThemeId()
{
    const auto raw = SettingsManager::getDefaultManager()
                         ->getValue(settingsKey(), defaultThemeId()).toString();
    if (raw == classicId()) return classicId();
    return qlementineDarkId();
}

void ThemeManager::setThemeId(const QString &id)
{
    SettingsManager::getDefaultManager()->setValue(settingsKey(), id);
}

bool ThemeManager::classicActive()
{
    return s_classicActive;
}

void ThemeManager::applyAtStartup(QApplication &app)
{
    s_classicActive = (currentThemeId() == classicId());
    StyleSheet::setClassicThemeActive(s_classicActive);

    if (s_classicActive) {
        // Jahshaka Classic (archived): no QStyle, per-widget stylesheets, DroidSans.
        // This is bit-for-bit the pre-Qlementine appearance path.
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        int id = QFontDatabase::addApplicationFont(":/fonts/DroidSans.ttf");
        if (id != -1) {
            QString family = QFontDatabase::applicationFontFamilies(id).at(0);
            QFont droid(family, Constants::UI_FONT_SIZE);
            droid.setStyleStrategy(QFont::PreferAntialias);
            QApplication::setFont(droid);
        }
#endif
        return;
    }

    // Qlementine Dark (the default): a real QStyle owns every stock widget.
    // Must run before any widget is constructed. Typography comes from the
    // theme (Inter/Roboto Mono, bundled by Qlementine) — no DroidSans override.
    auto *style = new JahQlementineStyle(&app);
    style->setThemeJsonPath(QStringLiteral(":/themes/jahshaka-dark.json"));
    QApplication::setStyle(style);
}

QString ThemeManager::chromeButtonSheet()
{
    if (s_classicActive) return QString();
    return themeSheet(QStringLiteral(
        "QPushButton, QToolButton { background: #444; color: #eee;"
        " padding: 8px 12px; border-radius: 4px; }"
        "QPushButton:hover, QToolButton:hover { background: #555; }"
        "QPushButton:pressed, QToolButton:pressed { background: #3a3a3a; }"
        "QPushButton:checked, QToolButton:checked { background: #2980b9; }"
        "QPushButton:disabled, QToolButton:disabled { background: #333; color: #777; }"));
}

QString ThemeManager::chromeAccentButtonSheet()
{
    if (s_classicActive) return QString();
    return themeSheet(QStringLiteral(
        "QPushButton { background: #3498db; color: white; padding: 8px 12px;"
        " border-radius: 4px; }"
        "QPushButton:hover { background: #4ba3e0; }"
        "QPushButton:pressed { background: #2884c4; }"
        "QPushButton:disabled { background: #24384a; color: #7d8fa3; }"));
}

QString ThemeManager::chromeCompactButtonSheet()
{
    if (s_classicActive) return QString();
    // chromeButtonSheet at reduced height: identical palette, radius and
    // 12px side gutters — only the vertical padding shrinks.
    return themeSheet(QStringLiteral(
        "QPushButton, QToolButton { background: #444; color: #eee;"
        " padding: 3px 12px; border-radius: 4px; }"
        "QPushButton:hover, QToolButton:hover { background: #555; }"
        "QPushButton:pressed, QToolButton:pressed { background: #3a3a3a; }"
        "QPushButton:checked, QToolButton:checked { background: #2980b9; }"
        "QPushButton:disabled, QToolButton:disabled { background: #333; color: #777; }"));
}

QColor ThemeManager::tileCaptionBarColor(bool openProject)
{
    // Classic is archived and renders bit-for-bit as it shipped: black in both
    // states. Only the Qlementine theme marks the open tile. (The kill-switch
    // flag, not s_classicActive: same value — applyAtStartup sets both from one
    // read — but this one is settable, so the suites can exercise both themes.)
    if (!openProject || StyleSheet::classicThemeActive()) return QColor(Qt::black);

    // The theme primary (#3498db, app/themes/jahshaka-dark.json) taken down to
    // ~40% value: unmistakably blue beside the black bars of every other tile,
    // and still ~12:1 contrast against the white caption text (the primary
    // itself is only ~2.9:1 under white and would fail to read).
    return QColor(0x14, 0x39, 0x5c);
}

QString ThemeManager::tileCaptionBarSheet(int fontSize, int cornerRadius, bool openProject)
{
    // Geometry identical for both states (owner-tuned: the top of the bar hugs
    // the text, the bottom gets two extra pixels) — only the background moves.
    return themeSheet(QStringLiteral("background-color: %1; color: white; font-size: %2px;"
                          " padding-bottom: 2px;"
                          " border-bottom-left-radius: %3px;"
                          " border-bottom-right-radius: %3px;")
        .arg(tileCaptionBarColor(openProject).name(),
             QString::number(fontSize),
             QString::number(cornerRadius)));
}

QString ThemeManager::headerGlyphButtonSheet(const QFont &iconFont)
{
    if (s_classicActive) return QString();
    // No plate, no border, no padding: the glyph IS the button. The hover
    // brightening is the only feedback (a plate around a 28px icon-font
    // character is what the owner read as a baked grey background).
    //
    // The FONT IS IN THE SHEET on purpose. A font pushed with setFont() does
    // not survive here: setStyleSheet() re-polishes the button, and both
    // Qlementine's polish (which re-sets the font of every QPushButton) and
    // Qt's own QStyleSheetStyle unpolish/polish pair restore the font they
    // saved earlier — which is how the Publish arrow, the one header glyph
    // whose sheet is re-applied after construction (updateTopMenuStates),
    // ended up rendering at the inherited 17px beside two 28px siblings. A
    // sheet-declared font wins every repolish, so all three stay identical.
    return themeSheet(QStringLiteral(
               "QPushButton { background: transparent; border: none; padding: 0;"
               " font-family: \"%1\"; font-size: %2px;"
               " color: rgba(255,255,255,0.9); }"
               "QPushButton:hover { color: rgba(255,255,255,1.0); }"
               "QPushButton:pressed { color: rgba(255,255,255,0.7); }")
        .arg(iconFont.family())
        .arg(iconFont.pixelSize() > 0 ? iconFont.pixelSize() : 28));
}

void ThemeManager::applyHeaderGlyphButton(QPushButton *button, const QFont &iconFont)
{
    if (!button) return;
    if (s_classicActive) {
        // Classic is bit-for-bit: its own sheet, its own setFont.
        button->setFont(iconFont);
        button->setStyleSheet(button->objectName() == QLatin1String("prefsButton")
                                  ? StyleSheet::PrefsButton()
                                  : StyleSheet::HelpButton());
        return;
    }
    button->setFont(iconFont);            // sizeHint before the first polish
    button->setStyleSheet(headerGlyphButtonSheet(iconFont));
}

void ThemeManager::switchifyMenuToggles(QMenu *menu)
{
    if (s_classicActive || !menu) return;

    const auto actions = menu->actions();
    for (QAction *action : actions) {
        if (!action->isCheckable() || qobject_cast<QWidgetAction *>(action))
            continue;

        auto *container = new QWidget(menu);
        auto *lay = new QHBoxLayout(container);
        lay->setContentsMargins(12, 4, 12, 4);
        auto *sw = new oclero::qlementine::Switch(container);
        sw->setText(action->text());
        sw->setChecked(action->isChecked());
        lay->addWidget(sw);

        QObject::connect(sw, &QAbstractButton::toggled, action, &QAction::setChecked);
        QObject::connect(action, &QAction::toggled, sw, &QAbstractButton::setChecked);

        auto *wa = new QWidgetAction(menu);
        wa->setDefaultWidget(container);
        menu->insertAction(action, wa);
        menu->removeAction(action);
    }
}

QString ThemeManager::topMenuButtonSheet(TopMenuState state)
{
    if (s_classicActive) return QString();
    // The geometry the header was designed around (the archived root sheet's
    // #worlds_menu block): 17px labels, 14px padding, a 4px bottom band that
    // keeps the header's height, no plate.
    const char *color = "#eeeeee";
    const char *hover = "#ffffff";
    if (state == TopMenuState::Active) { color = "#3498db"; hover = "#4ba3e0"; }
    if (state == TopMenuState::Disabled) { color = "#63676d"; hover = "#63676d"; }
    return themeSheet(QStringLiteral(
               "QPushButton { background: transparent; border: none;"
               " border-bottom: 4px solid transparent; border-radius: 0px;"
               " padding: 14px; font-size: 17px; color: %1; }"
               "QPushButton:hover { color: %2; }")
        .arg(QLatin1String(color), QLatin1String(hover)));
}

void ThemeManager::applyTopMenuButton(QPushButton *button, TopMenuState state)
{
    if (!button) return;
    if (s_classicActive) {
        button->setStyleSheet(state == TopMenuState::Active     ? StyleSheet::TopMenuSelected()
                              : state == TopMenuState::Disabled ? StyleSheet::TopMenuDisabled()
                                                                : StyleSheet::TopMenuUnselected());
        return;
    }
    button->setStyleSheet(topMenuButtonSheet(state));
}

QString ThemeManager::sampleTileListSheet()
{
    return themeSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { background: black; color: white; }"
        "QListWidget::item:selected { background: #3498db; color: white; }"));
}

QString ThemeManager::accentGlyphButtonSheet()
{
    // Verbatim the sheet the Assets page's "+" carried in both themes before
    // the sweep (Classic keeps it bit-for-bit through here).
    return themeSheet(QStringLiteral(
        "QPushButton { background: #3498db; color: #FFFFFF; border-radius: 2px;"
        "              padding: 0; margin: 0; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background: #4EA8E5; }"));
}

QString ThemeManager::dropZoneSheet(const QString &paneName, const QString &labelName)
{
    if (s_classicActive) return QString();
    return themeSheet(QStringLiteral(
               "#%1 { border: 2px dashed #4a4a4a; border-radius: 6px; }"
               // the caption gives the target its height — a real target, not a strip
               "#%2 { padding: 24px 8px; }")
        .arg(paneName, labelName));
}

void ThemeManager::applyWindowFont(QWidget *window)
{
    if (!s_classicActive || !window) return;
    QFont font;
    font.setFamily(font.defaultFamily());
    window->setFont(font);
}

bool ThemeManager::isThemeSheet(const QString &sheet)
{
    return !sheet.isEmpty() && themeSheetRegistry().contains(sheet);
}
