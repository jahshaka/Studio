/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef JAH_THEMEMANAGER_H
#define JAH_THEMEMANAGER_H

#include <QColor>
#include <QString>

class QApplication;
class QFont;
class QWidget;

// App-wide theme selection (THEME_AUDIT.md §4). Two themes exist:
//
//   "qlementine-dark"  — the default: the Qlementine QStyle (thirdparty/qlementine)
//                        with the Jahshaka Dark JSON theme. All classic StyleSheet::
//                        getters are neutralized (return "") so the style owns rendering.
//   "classic"          — "Jahshaka Classic (archived)": today's per-widget stylesheets,
//                        bit-for-bit. Kept selectable for comparison while the classic
//                        crud is cleaned out; will be deleted eventually.
//
// The choice persists in jahsettings.ini under appearance/theme and takes effect
// at the NEXT start (restart-on-change — classic sheets are pushed at construction
// from hundreds of sites and cannot be cleanly un-applied live).
class ThemeManager
{
public:
    static QString settingsKey();     // "appearance/theme"
    static QString qlementineDarkId() { return QStringLiteral("qlementine-dark"); }
    static QString classicId()        { return QStringLiteral("classic"); }
    static QString defaultThemeId()   { return qlementineDarkId(); }

    // The persisted choice (normalized: anything unknown maps to the default).
    static QString currentThemeId();
    static void setThemeId(const QString &id);

    // True when the archived classic theme was applied at startup.
    static bool classicActive();

    // Reads the persisted choice and applies it. MUST run after the QApplication
    // is constructed and before ANY widget (the Upgrader dialog is the first).
    static void applyAtStartup(QApplication &app);

    // (clearClassicSheets — a recursive "wipe every sheet under this widget"
    // for Qlementine mode — was deleted by the theme sweep, platform audit
    // F-S1: it never had a caller, and after the sweep there is nothing left
    // for it to clear. Classic CSS lives only in StyleSheet:: getters that
    // return "" under Qlementine, and the few sheets Qlementine does carry are
    // this class's own chrome — exactly what a blanket sweeper would have
    // wiped. app.styleSheets / theme.sheets is the guard instead.)

    // THE chrome button spec (owner direction, one definition for all page
    // chrome): grey rounded button matching the blue accent buttons'
    // geometry — same padding, same bevel, horizontal text gutters. Used by
    // the desktop footer and the editor toolbar (and future chrome rows).
    // Returns "" under Classic, whose .ui/classic sheets already style these.
    static QString chromeButtonSheet();

    // The blue accent variant of the same spec (primary actions: New Scene,
    // Add to Project, Update). Same geometry, primary color.
    static QString chromeAccentButtonSheet();

    // Compact variant of the chrome spec for dense panel header rows (the
    // editor asset panel's Go Up / Display controls): same colors, rounded
    // corners and side gutters as chromeButtonSheet, reduced height. Returns
    // "" under Classic, like the full-height spec.
    static QString chromeCompactButtonSheet();

    // The header's GLYPH buttons (Publish / Help / Preferences): an icon-font
    // character on the header bar and nothing else — no button plate, no
    // border, no padding. Qlementine paints every QPushButton with its plate
    // background (#333), which on the near-black header reads as a "grey
    // background baked into the icon" (owner report 2026-09-07); this is the
    // sheet that takes the plate away. It also CARRIES THE ICON FONT: a font
    // pushed with setFont() does not survive a repolish (Qlementine's polish
    // re-sets every QPushButton's font, and Qt's stylesheet style restores the
    // font it saved), which is exactly how the Publish arrow — the one header
    // glyph whose sheet is re-applied after construction, in
    // updateTopMenuStates — ended up 17px beside two 28px siblings. "" under
    // Classic, whose HelpButton() / PrefsButton() sheets already do it.
    static QString headerGlyphButtonSheet(const QFont &iconFont);

    // Apply the glyph-button look and the icon font to `button`, per theme.
    // One call site for all three header glyphs, so they cannot drift apart.
    static void applyHeaderGlyphButton(class QPushButton *button,
                                       const QFont &iconFont);

    // THE DESKTOP TILE'S CAPTION BAR — the band under a project thumbnail that
    // carries the project name (src/ui/controls/itemgridwidget.cpp). Ordinary
    // tiles keep the black band they always had; the tile of the project that
    // is currently OPEN gets the theme's dark blue instead, so the open project
    // is spottable across a full desktop (owner request 2026-09-08). Under
    // Classic BOTH states stay black — that theme is archived and must render
    // bit-for-bit as it shipped.
    static QColor tileCaptionBarColor(bool openProject);

    // The whole caption-bar sheet built around that colour: the geometry
    // (font size, bottom padding, the two bottom corner radii that close the
    // tile card) is IDENTICAL for both states, so nothing but the background
    // moves when a project opens.
    static QString tileCaptionBarSheet(int fontSize, int cornerRadius, bool openProject);

    // Qlementine mode: replaces every checkable QAction in the menu with a
    // qlementine Switch row (QWidgetAction). The original QAction stays alive
    // and authoritative — the switch and the action mirror each other — so
    // code toggling the action programmatically keeps working. No-op under
    // Classic. Call once, after the menu's actions are all added.
    static void switchifyMenuToggles(class QMenu *menu);

    // True when `sheet` is exactly a sheet this class handed out (every
    // getter above records what it returns). The live theme walk
    // (app.styleSheets) classifies each widget sheet with this: under
    // Qlementine a non-empty sheet that is NOT the theme's own is raw crud.
    static bool isThemeSheet(const QString &sheet);

    // THE SPACE MENU — the header's text buttons (Desktop / Player / Editor /
    // Materials / Assets / Avatar). The header is ours, not a stock control:
    // flat labels on the near-black band, the ACTIVE space in the accent
    // colour, the editor/player entries greyed while no scene is open. Under
    // Qlementine one sheet per state carries the whole look (geometry, font
    // size, colour) — the text colour cannot come from the palette, since the
    // style paints button text from its theme. Classic keeps its archived
    // border-colour swap (TopMenuSelected / Unselected / Disabled).
    enum class TopMenuState { Idle, Active, Disabled };
    static QString topMenuButtonSheet(TopMenuState state);
    static void applyTopMenuButton(class QPushButton *button, TopMenuState state);

    // The sample browser's tile list (Desktop ▸ Sample Scenes): the desktop
    // tiles' look — thumbnail over a black name band, accent when selected —
    // in BOTH themes (verbatim what the list carried before the sweep).
    static QString sampleTileListSheet();

    // A small square glyph button in the accent colour (the Assets page's
    // "+" new-drawer button): the same look in BOTH themes — Classic shipped it
    // as a raw sheet, and a Qlementine button cannot take its colour from the
    // palette. 16px bold glyph, no padding (a 24px button has no room for any).
    static QString accentGlyphButtonSheet();

    // A drop target's affordance: a dashed rounded outline around the pane
    // that accepts files (the Assets page's drop pad). "" under Classic,
    // whose page sheet draws its own box. `labelName` is the objectName of the
    // pane's caption label, which gets the target's vertical room.
    static QString dropZoneSheet(const QString &paneName, const QString &labelName);

    // The main window's font (platform audit F-S3). Under Qlementine the
    // theme owns typography — nothing is set, the window inherits the
    // theme's font. Classic keeps what it always rendered: the platform's
    // default family at the application's point size (the old code also
    // multiplied the POINT size by the device pixel ratio — a point size is
    // already device-independent, so that doubled every font on a 2x display;
    // at 1x, the only ratio Classic was ever judged at, dropping it is
    // bit-for-bit).
    static void applyWindowFont(QWidget *window);
};

#endif // JAH_THEMEMANAGER_H
