// theme.manager — unit test of the app theme selection (THEME_AUDIT.md):
// the appearance/theme setting round-trip and normalization, and the
// StyleSheet kill switch that neutralizes every classic getter when the
// Qlementine QStyle owns rendering. Runs offscreen; no style is applied.

#include <QApplication>
#include <QFocusFrame>
#include <QFont>
#include <QImage>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>
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

    // ---- the focus frame survives an ANCESTOR being reparented ---------------
    // Qlementine draws focus OUTSIDE a widget's bounds with a QFocusFrame, and
    // QFocusFrame::setWidget() caches the frame's parent by walking up to the
    // enclosing scroll area's viewport (SH_FocusFrame_AboveWidget, which this
    // style turns on). Qt refreshes that choice when the WIDGET's own parent
    // changes but not when an INTERMEDIATE ANCESTOR is reparented — and
    // SceneNodePropertiesWidget::clearLayout() does exactly that: it reuses the
    // property blades and orphans them with setParent(nullptr) instead of
    // deleting them. Before the 2026-09-08 fix in the (now vendored) filter,
    // the frame stayed behind in the viewport and QFocusFramePrivate::updateSize()
    // mapped coordinates across two unrelated widget trees on every geometry
    // change: "QWidget::mapTo(): parent must be in parent hierarchy",
    // 164,651 times in one 13-minute session.
    {
        settings->settings->remove(ThemeManager::settingsKey());
        ThemeManager::applyAtStartup(app);

        // The scroll area is NESTED, as it is in the real panel: Qt's walk
        // stops at the first window, so a top-level scroll area would park the
        // frame on the scroll area itself instead of on its viewport.
        QWidget host;
        host.setLayout(new QVBoxLayout);
        auto *scrollPtr = new QScrollArea(&host);
        QScrollArea &scroll = *scrollPtr;
        host.layout()->addWidget(scrollPtr);
        auto *content = new QWidget;
        content->setLayout(new QVBoxLayout);
        scroll.setWidget(content);
        scroll.setWidgetResizable(true);

        // panel -> blade -> row -> button, the shape the properties panel builds.
        auto *blade = new QWidget;
        blade->setLayout(new QVBoxLayout);
        auto *row = new QWidget(blade);
        row->setLayout(new QVBoxLayout);
        auto *button = new QPushButton(QStringLiteral("probe"), row);
        row->layout()->addWidget(button);
        blade->layout()->addWidget(row);
        content->layout()->addWidget(blade);

        host.resize(320, 240);
        host.show();
        // The filter attaches the frame one event-loop turn after the widget's
        // FIRST PAINT, so the paint has to actually happen (the offscreen QPA
        // never flushes one on its own) and the queue has to turn afterwards.
        auto settle = [&]() {
            for (int i = 0; i < 10; ++i) {
                host.grab();
                QApplication::processEvents();
                QApplication::sendPostedEvents();
            }
        };
        settle();

        auto *frame = button->window()->findChild<QFocusFrame *>();
        CHECK(frame != nullptr && frame->widget() == button,
              "focus frame: qlementine attached one to the button");
        QWidget *const viewport = scroll.viewport();
        CHECK(frame && frame->parentWidget() == viewport,
              "focus frame: upstream parents it to the scroll area's viewport");

        // PIXELS, not just geometry: the ring is what a user sees. Grab the
        // panel unfocused and focused — the difference IS the ring, so a ring
        // that stopped painting shows up here and not only in a rectangle.
        const QImage unfocused = host.grab().toImage();
        button->setFocus(Qt::TabFocusReason);
        host.setAttribute(Qt::WA_KeyboardFocusChange, true);
        settle();
        const QImage focused = host.grab().toImage();
        CHECK(!focused.isNull() && focused != unfocused,
              "focus frame: the ring actually paints around a focused button");

        // Count the exact warning Qt emits from QFocusFramePrivate::updateSize().
        static int mapToWarnings = 0;
        mapToWarnings = 0;
        auto *prev = qInstallMessageHandler(
            [](QtMsgType, const QMessageLogContext &, const QString &msg) {
                if (msg == QLatin1String("QWidget::mapTo(): parent must be in parent hierarchy"))
                    ++mapToWarnings;
            });

        // Orphan the blade exactly the way clearLayout() does, then make the
        // detached subtree do geometry work.
        content->layout()->removeWidget(blade);
        blade->setParent(nullptr);
        settle();
        // Resize the LIVE side. Qt filters the widgets between the button and
        // the frame's parent — the viewport included — and answers any of their
        // Move/Resize events with updateSize(), which is where the stale
        // ancestry gets mapped (the app's stack: dock layout -> scroll area ->
        // viewport setGeometry -> QFocusFrame::eventFilter -> mapTo).
        host.resize(420, 300);
        settle();
        host.resize(320, 240);
        settle();

        qInstallMessageHandler(prev);
        CHECK(mapToWarnings == 0,
              "focus frame: an orphaned ancestor produces NO mapTo warnings");
        CHECK(frame && frame->parentWidget() != viewport,
              "focus frame: it followed the widget out of the scroll area");

        // Re-attached, it must land back exactly where upstream put it — the
        // ring is drawn in the viewport's coordinates or it is drawn wrong.
        content->layout()->addWidget(blade);
        settle();
        CHECK(frame && frame->parentWidget() == viewport,
              "focus frame: re-attaching restores the viewport parent");
        // (No pixel compare across the round trip: QlementineStyle ANIMATES the
        // focus border width, and a re-shown frame restarts that animation from
        // zero — an offscreen harness has no running event loop to advance it,
        // so the ring is simply mid-fade here. Verified not to be a rendering
        // defect: the post-round-trip grab is byte-identical with and without
        // this fix. The geometry assertion below is what pins the placement.)
        CHECK(frame && frame->geometry()
                  == QRect(button->mapTo(viewport, QPoint(0, 0)), button->size())
                         .adjusted(-frame->style()->pixelMetric(QStyle::PM_FocusFrameHMargin),
                                   -frame->style()->pixelMetric(QStyle::PM_FocusFrameVMargin),
                                   frame->style()->pixelMetric(QStyle::PM_FocusFrameHMargin),
                                   frame->style()->pixelMetric(QStyle::PM_FocusFrameVMargin)),
              "focus frame: it sits on the button, margins and all");

        host.hide();
    }

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
