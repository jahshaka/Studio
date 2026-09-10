/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ROWUNDO_H
#define ROWUNDO_H

// rowundo — ONE GESTURE, ONE UNDO STEP, for every properties-panel row
// (debt L6 / N5, the Rayon sliders' pattern generalised).
//
// The rule the panels have to keep is always the same and is easy to get wrong
// in twelve different ways:
//
//   * a DRAG is one edit, not one per pixel — the document must follow the
//     drag live (the viewport is the feedback), but the undo stack must get a
//     single step when the mouse comes up;
//   * a TYPED value is one edit, committed on Return or focus-out;
//   * a checkbox, a combo or a picker is one edit the moment it changes;
//   * a gesture that ends where it started is NOT an edit and must leave the
//     stack alone (press-and-release on a slider, a cancelled colour dialog,
//     Return on an unchanged field);
//   * and the row's own population — `setValue()` while a node is being
//     selected — must never look like a user edit at all. HFloatSliderWidget
//     emits valueChanged from setValue(), so this is a real hazard, not a
//     theoretical one; the before/after comparison below is the second line of
//     defence behind each panel's loading guard.
//
// A Binding says how one row reaches the document: `read` gives its current
// document value, `write` applies a value live, `commit` turns a (before,
// after) pair into an undo step — a SetNodePropertyCommand, a
// ScenePropertyCommand, a WorldModeCommand, whatever that row's model is.
// rowundo owns only the GESTURE bookkeeping, which is the part every panel got
// wrong by not having it.
//
// The value carried through `write` is the control's own natural type:
//   slider -> float · drag float -> double · drag vector -> QVector3D ·
//   colour -> QColor · check box -> bool · combo -> int (the row index).
// Mapping that onto the document (a row index onto an enum, a fraction onto an
// absolute) is the panel's job, in its own lambda, where the domain knowledge
// already lives.

#include <functional>

#include <QVariant>

class HFloatSliderWidget;
class DragFloatWidget;
class DragVector3Widget;
class ColorPickerWidget;
class CheckBoxWidget;
class ComboBoxWidget;

namespace rowundo {

struct Binding
{
    /// False while the panel is POPULATING its rows rather than showing a user
    /// edit. HFloatSliderWidget::setValue and ColorPickerWidget::setColor both
    /// emit, so without this a selection change would write every row's value
    /// back into the document and (with undo) record steps for edits nobody
    /// made. Optional; omitted = always live.
    std::function<bool()> guard;
    /// The row's CURRENT value as the document holds it.
    std::function<QVariant()> read;
    /// Applies a control value to the document, live (every tick of a drag).
    std::function<void(const QVariant &)> write;
    /// Pushes the undo step for a finished gesture. Never called when the
    /// value did not move.
    std::function<void(const QVariant &before, const QVariant &after)> commit;
};

/// Slider rows: bracketed by the control's own valueChangeStart/End (a drag,
/// or a keyboard editing session). A tick outside a bracket is its own step.
void bind(HFloatSliderWidget *slider, const Binding &binding);

/// Scrubbable number rows: the first tick opens the session, `editingDone`
/// closes it (the control has no start signal).
void bind(DragFloatWidget *field, const Binding &binding);
void bind(DragVector3Widget *field, const Binding &binding);

/// Colour rows: the popup's pickingStarted/pickingEnded bracket the live
/// stream; a direct set (onSetColor) is one atomic edit.
void bind(ColorPickerWidget *picker, const Binding &binding);

/// One-shot rows.
void bind(CheckBoxWidget *box, const Binding &binding);
void bind(ComboBoxWidget *combo, const Binding &binding);

}   // namespace rowundo

#endif   // ROWUNDO_H
