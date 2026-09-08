/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ROWFIT_H
#define ROWFIT_H

// A PROPERTY ROW FITS ITS DOCK (owner report 2026-09-08: "the settings run off
// the right side of the panel and I can't see some of them").
//
// The Properties dock hosts its panel in a QScrollArea with
// widgetResizable(true) and Qt::ScrollBarAlwaysOff for the horizontal
// direction. That combination has one consequence people keep re-discovering:
// content whose MINIMUM width exceeds the viewport is neither scrolled nor
// shrunk, it is CLIPPED — and the clipped end of a property row is the value,
// the control, the thing the row exists for.
//
// A default Qt row is exactly that kind of content. A QLabel's minimum width is
// the full width of its text (it does not elide), a QComboBox's is the widest
// item it holds (AdjustToContentsOnFirstShow), a spin box's is its widest
// number plus its buttons. Add a long section name — "Ambient From Sky",
// "Instant Radiosity + VCT (hybrid)" — and one row alone can demand 450 px of a
// 368 px dock, taking the whole panel with it.
//
// These helpers give a row the size behaviour it should have had: every part of
// it can shrink, and the parts that hold text elide instead of overflowing. The
// numbers stay readable, the names lose their tails first, and nothing is ever
// hidden past the right edge.
//
// The choke point is AccordianBladeWidget: every row it adds to a blade goes
// through fitRow(), including the ones panels build themselves and hand to
// addWidgetToContent(). Row classes do NOT have to opt in.

class QWidget;
class QLabel;
class QComboBox;
class QAbstractSpinBox;
class QLineEdit;

namespace RowFit {

/// The floor a fitted control is allowed to shrink to. Small on purpose: the
/// dock's minimum width is the real contract (ui/style/panelmetrics.h), and a
/// control that keeps a generous minimum of its own is exactly what breaks it.
constexpr int kMinControlWidth = 36;

/// Elides (with a tooltip carrying the full text) instead of forcing width.
/// Re-elides on every resize and picks up later setText() calls, so a panel
/// that rebuilds its rows needs no cooperation.
void fitLabel(QLabel *label);

/// Minimum width stops being "the widest item"; the current text elides.
void fitCombo(QComboBox *combo);

/// The number keeps its buttons and scrolls its text; the row stops being as
/// wide as the spin box's widest possible value.
void fitSpin(QAbstractSpinBox *spin);

void fitLineEdit(QLineEdit *edit);

/// Applies the right one of the above to `row` and to every descendant.
/// Idempotent — a row may be fitted more than once.
void fitRow(QWidget *row);

}   // namespace RowFit

#endif // ROWFIT_H
