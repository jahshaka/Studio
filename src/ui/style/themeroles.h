/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef JAH_THEMEROLES_H
#define JAH_THEMEROLES_H

// PALETTE AND FONT ROLES — the sheet-free way to say "muted text", "a heading",
// "the darker header band" under the Qlementine theme (theme sweep, lane 16).
//
// Why not a stylesheet: ANY non-empty sheet on a widget interposes
// QStyleSheetStyle over the Qlementine QStyle for that widget and its whole
// subtree (THEME_AUDIT.md §3) — the hybrids the sweep removed. A palette role
// or a font is read by the style itself, so the widget stays native.
//
// Every call is a NO-OP under the archived Classic theme: Classic styles the
// same widgets through its StyleSheet:: getters (classicsheets.cpp), and must
// render bit-for-bit as it shipped. So a call site can be unconditional.
//
// Colours come from the application palette the Qlementine theme installed
// (app/themes/jahshaka-dark.json → Theme::palette) where a role carries them;
// the three status colours and the surfaces below have no palette role and
// mirror the same JSON's keys, named beside each constant.
//
// Header-only on purpose: every TU that already links stylesheet.cpp can use
// it without a new source file or the qlementine library.

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QPushButton>
#include <QToolButton>
#include <QWidget>

#include "ui/style/stylesheet.h"

namespace ThemeRoles {

enum class Tone {
    Normal,    // the theme's text colour (secondaryColor, #eeeeee)
    Muted,     // secondary text (secondaryAlternativeColor, #9a9a9a — palette BrightText)
    Faint,     // placeholder-strength text (secondaryColorDisabled — palette PlaceholderText)
    Accent,    // the primary colour (#3498db — palette Highlight)
    Warning,   // statusColorWarning  #fbc064
    Error,     // statusColorError    #e96b72
    Success    // statusColorSuccess  #2bb5a0
};

inline QColor toneColor(Tone tone)
{
    const QPalette app = QApplication::palette();
    switch (tone) {
    case Tone::Normal:  return app.color(QPalette::Active, QPalette::WindowText);
    case Tone::Muted:   return app.color(QPalette::Active, QPalette::BrightText);
    case Tone::Faint:   return app.color(QPalette::Active, QPalette::PlaceholderText);
    case Tone::Accent:  return app.color(QPalette::Active, QPalette::Highlight);
    case Tone::Warning: return QColor(0xfb, 0xc0, 0x64);
    case Tone::Error:   return QColor(0xe9, 0x6b, 0x72);
    case Tone::Success: return QColor(0x2b, 0xb5, 0xa0);
    }
    return app.color(QPalette::Active, QPalette::WindowText);
}

/// Text colour for a LABEL-like widget (QLabel, QCheckBox text, item views).
/// Qlementine paints push-button text from its theme, never the palette — a
/// coloured button needs a ThemeManager sheet, not this.
inline void setTone(QWidget *w, Tone tone)
{
    if (!w || StyleSheet::classicThemeActive()) return;
    QPalette pal = w->palette();
    const QColor c = toneColor(tone);
    for (QPalette::ColorGroup g : { QPalette::Active, QPalette::Inactive }) {
        pal.setColor(g, QPalette::WindowText, c);
        pal.setColor(g, QPalette::Text, c);
    }
    w->setPalette(pal);
}

enum class Surface {
    Header,     // the near-black band behind the space menu (#111111)
    Workspace,  // backgroundColorWorkspace  #151515
    Panel,      // backgroundColorMain1      #1e1e1e
    Raised,     // backgroundColorMain2      #262626 (the palette's Window)
    Warning,    // a warning banner's amber  #7a4a12 (pairs with Tone::Normal text)
    Black       // video / render letterbox  #000000
};

inline QColor surfaceColor(Surface surface)
{
    switch (surface) {
    case Surface::Header:    return QColor(0x11, 0x11, 0x11);
    case Surface::Workspace: return QColor(0x15, 0x15, 0x15);
    case Surface::Panel:     return QColor(0x1e, 0x1e, 0x1e);
    case Surface::Raised:    return QColor(0x26, 0x26, 0x26);
    case Surface::Warning:   return QColor(0x7a, 0x4a, 0x12);
    case Surface::Black:     return QColor(0x00, 0x00, 0x00);
    }
    return QColor(0x1e, 0x1e, 0x1e);
}

/// An opaque background for a container: palette Window + autoFillBackground.
inline void setSurface(QWidget *w, Surface surface)
{
    if (!w || StyleSheet::classicThemeActive()) return;
    QPalette pal = w->palette();
    pal.setColor(QPalette::Window, surfaceColor(surface));
    w->setPalette(pal);
    w->setAutoFillBackground(true);
}

/// A text size in PIXELS, keeping the theme's family (the theme owns
/// typography; a page only says "bigger" or "bolder").
inline void setTextSize(QWidget *w, int pixelSize, QFont::Weight weight = QFont::Normal)
{
    if (!w || StyleSheet::classicThemeActive()) return;
    QFont f = w->font();
    f.setPixelSize(pixelSize);
    f.setWeight(weight);
    w->setFont(f);
}

/// An icon/text button with no plate until hovered — the style's own flat
/// button (QPushButton::flat, QToolButton::autoRaise), where Classic used a
/// "background: transparent" sheet.
inline void setFlat(QAbstractButton *b)
{
    if (!b || StyleSheet::classicThemeActive()) return;
    if (auto *push = qobject_cast<QPushButton *>(b)) push->setFlat(true);
    else if (auto *tool = qobject_cast<QToolButton *>(b)) tool->setAutoRaise(true);
}

} // namespace ThemeRoles

#endif // JAH_THEMEROLES_H
