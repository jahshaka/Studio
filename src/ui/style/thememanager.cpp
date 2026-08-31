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
#include "data/settingsmanager.h"
#include "data/constants.h"

#include <QApplication>
#include <QFontDatabase>

#include <oclero/qlementine/style/QlementineStyle.hpp>

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QMenu>
#include <QMetaObject>
#include <QPointer>
#include <QWidgetAction>

#include <oclero/qlementine/widgets/Switch.hpp>

static bool s_classicActive = false;

namespace {

// Qt 6.10 + Qlementine v1.4.2 crash workaround (verified by backtrace,
// 2026-08-31): QComboBoxPrivateContainer's constructor calls ensurePolished()
// BEFORE QComboBoxPrivate::container is assigned. QlementineStyle::polish of
// the popup's item view reparents the popup (setWindowFlag) and installs a
// ComboboxItemViewFilter whose ChildAdded handler calls QComboBox::view() —
// which, container still null, constructs a SECOND container, recursing until
// the stack overflows on the first QComboBox::addItem of the app (upstream dev
// branch has the same code). Deferring that one polish by an event-loop tick
// lets the container pointer land first; the deferred polish then behaves
// exactly as upstream intended. Everything else passes straight through.
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

        if (auto *itemView = qobject_cast<QAbstractItemView *>(w)) {
            auto *popup = itemView->parentWidget();
            if (popup && popup->inherits("QComboBoxPrivateContainer")) {
                QPointer<QWidget> guard(itemView);
                QMetaObject::invokeMethod(
                    this,
                    [this, guard]() {
                        if (guard) oclero::qlementine::QlementineStyle::polish(guard.data());
                    },
                    Qt::QueuedConnection);
                return;
            }
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
        oclero::qlementine::QlementineStyle::drawControl(element, opt, p, w);
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
    return QStringLiteral(
        "QPushButton, QToolButton { background: #444; color: #eee;"
        " padding: 8px 12px; border-radius: 4px; }"
        "QPushButton:hover, QToolButton:hover { background: #555; }"
        "QPushButton:pressed, QToolButton:pressed { background: #3a3a3a; }"
        "QPushButton:checked, QToolButton:checked { background: #2980b9; }"
        "QPushButton:disabled, QToolButton:disabled { background: #333; color: #777; }");
}

QString ThemeManager::chromeAccentButtonSheet()
{
    if (s_classicActive) return QString();
    return QStringLiteral(
        "QPushButton { background: #3498db; color: white; padding: 8px 12px;"
        " border-radius: 4px; }"
        "QPushButton:hover { background: #4ba3e0; }"
        "QPushButton:pressed { background: #2884c4; }"
        "QPushButton:disabled { background: #24384a; color: #7d8fa3; }");
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

void ThemeManager::clearClassicSheets(QWidget *root)
{
    if (s_classicActive || !root) return;

    if (!root->styleSheet().isEmpty()) root->setStyleSheet(QString());
    const auto children = root->findChildren<QWidget *>();
    for (auto *w : children)
        if (!w->styleSheet().isEmpty()) w->setStyleSheet(QString());
}
