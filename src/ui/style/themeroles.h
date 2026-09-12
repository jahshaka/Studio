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
#include <QEvent>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFrame>
#include <QPalette>
#include <QPixmap>
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
    Band,       // neutralColor              #3a3a3a (the palette's Button) — section headers
    Warning,    // a warning banner's amber  #7a4a12 (pairs with Tone::Normal text)
    Black,      // video / render letterbox  #000000
    Scrim       // a 55% black veil over content (a tile's loading overlay)
};

inline QColor surfaceColor(Surface surface)
{
    switch (surface) {
    case Surface::Header:    return QColor(0x11, 0x11, 0x11);
    case Surface::Workspace: return QColor(0x15, 0x15, 0x15);
    case Surface::Panel:     return QColor(0x1e, 0x1e, 0x1e);
    case Surface::Raised:    return QColor(0x26, 0x26, 0x26);
    case Surface::Band:      return QApplication::palette().color(QPalette::Active, QPalette::Button);
    case Surface::Warning:   return QColor(0x7a, 0x4a, 0x12);
    case Surface::Black:     return QColor(0x00, 0x00, 0x00);
    case Surface::Scrim:     return QColor(0, 0, 0, 140);
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

/// No background of its own: the parent shows through (a scroll area's
/// viewport otherwise fills with the palette's Base).
inline void clearBackground(QWidget *w)
{
    if (!w || StyleSheet::classicThemeActive()) return;
    w->setAutoFillBackground(false);
}

/// A frame shape the style draws (Qlementine: NoFrame, or its rounded
/// StyledPanel) — where Classic set `border:` in a sheet.
inline void setFrame(QFrame *frame, QFrame::Shape shape)
{
    if (!frame || StyleSheet::classicThemeActive()) return;
    frame->setFrameShape(shape);
}

namespace detail {
// Re-stretches a container's background picture whenever the container is
// resized (its size at construction is rarely its final size).
class BackgroundPixmapFilter : public QObject
{
public:
    BackgroundPixmapFilter(QWidget *w, const QPixmap &pixmap) : QObject(w), mPixmap(pixmap) { apply(w); }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize) apply(static_cast<QWidget *>(watched));
        return false;
    }

private:
    void apply(QWidget *w)
    {
        if (w->width() <= 0 || w->height() <= 0) return;
        QPalette pal = w->palette();
        pal.setBrush(QPalette::Window,
                     QBrush(mPixmap.scaled(w->size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)));
        w->setPalette(pal);
    }
    QPixmap mPixmap;
};
} // namespace detail

/// A picture as a container's background, stretched to the container's size
/// and re-stretched on every resize (Classic's `border-image:` — a dialog's art).
inline void setBackgroundPixmap(QWidget *w, const QPixmap &pixmap)
{
    if (!w || pixmap.isNull() || StyleSheet::classicThemeActive()) return;
    w->setAutoFillBackground(true);
    w->installEventFilter(new detail::BackgroundPixmapFilter(w, pixmap));
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

/// Breathing room around a label's text — what a Classic sheet said with
/// `padding:` (a sheet's padding and contents margins would add up under
/// Classic, so this, too, is Qlementine-only).
inline void setPadding(QWidget *w, int left, int top, int right, int bottom)
{
    if (!w || StyleSheet::classicThemeActive()) return;
    w->setContentsMargins(left, top, right, bottom);
}

/// A monospace face at a pixel size: the theme's bundled Roboto Mono when it
/// is registered (Qlementine ships it), else the platform's fixed font.
inline void setMonospace(QWidget *w, int pixelSize)
{
    if (!w || StyleSheet::classicThemeActive()) return;
    QFont f(QStringLiteral("Roboto Mono"));
    f.setStyleHint(QFont::Monospace);
    if (!QFontInfo(f).fixedPitch()) f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPixelSize(pixelSize);
    w->setFont(f);
}

/// An icon/text button with no plate until hovered — the style's own flat
/// button (QPushButton::flat, QToolButton::autoRaise), where Classic used a
/// "background: transparent" sheet.
// ---------------------------------------------------------------------------
// ITEM VIEWS WHOSE ACTIVATION IS EXPENSIVE (AV1, owner 2026-09-13)
// ---------------------------------------------------------------------------
// Qlementine answers SH_ItemView_ActivateItemOnSingleClick with TRUE, and
// QAbstractItemView emits `activated` on the release of ANY button when it
// does — including the RIGHT button that was only meant to raise a context
// menu. On a list whose activation LOADS A CHARACTER that is the owner's
// report: "I right click and it starts reloading her... so I can't access the
// menu". Views marked here get the platform-default rule instead — a click
// selects, a double-click or Enter activates — and their context menus work.
//
// Marked, not hard-coded, so the rule is a property of the VIEW (its
// activation is expensive), read by JahQlementineStyle::styleHint in
// thememanager.cpp. A no-op under Classic, whose base style already answers 0.
inline const char *activateOnDoubleClickProperty() { return "jahActivateOnDoubleClick"; }

inline void setActivateOnDoubleClick(QWidget *view)
{
    if (view) view->setProperty(activateOnDoubleClickProperty(), true);
}

inline void setFlat(QAbstractButton *b)
{
    if (!b || StyleSheet::classicThemeActive()) return;
    if (auto *push = qobject_cast<QPushButton *>(b)) push->setFlat(true);
    else if (auto *tool = qobject_cast<QToolButton *>(b)) tool->setAutoRaise(true);
}

} // namespace ThemeRoles

#endif // JAH_THEMEROLES_H
