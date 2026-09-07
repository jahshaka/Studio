/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SINGLEDRAGOWNER_H
#define SINGLEDRAGOWNER_H

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPointF>

/// ONE DRAG OWNER PER VIEW — the structural end of the "sticky preset" defect
/// (owner report, THIRD recurrence 2026-09-07).
///
/// THE SHAPE OF THE BUG. Three item views in this app (the model preset panel,
/// the material preset panel, the asset browser) run their OWN QDrag from an
/// event filter on the view's viewport, because the payload they need is not
/// the model's default mime data. But the views were also left with the
/// built-in drag machinery armed (`setDragDropMode(DragDrop)` turns
/// `dragEnabled` on as a side effect), so TWO owners raced over one gesture:
///
///   * `QAbstractItemView::mouseMoveEvent` puts the view into DraggingState as
///     soon as the mouse moves over a valid index with a button down, and on
///     the NEXT move past the drag distance it calls `startDrag()`. That is the
///     DOUBLE-ADD half — one gesture, two QDrags, two drops, two nodes, two
///     undo entries (traced 2026-09-07: source=AssetModelPanel then
///     source=QListWidget). The event filter's `return true` on the move that
///     starts our drag closes that half.
///   * The STICKY half is what `return true` cannot reach. The view is already
///     in DraggingState when our QDrag::exec() opens its nested loop, and the
///     release that ends the drag is consumed in there — the view never sees
///     it, so it stays in DraggingState forever. Re-enter the panel later with
///     NO BUTTON HELD and the very first mouse move takes the branch at the
///     top of mouseMoveEvent (it tests the state, not the buttons) and starts
///     a drag out of nothing: the item glues itself to the cursor.
///
/// THE FIX, both halves, at the source: the view must not own a drag at all.
///   1. `disarmViewDrag` clears `dragEnabled` — AFTER the drag/drop mode, which
///      is what sets it — so DraggingState can never be entered again. Drops
///      are untouched: a view that accepts them keeps accepting them.
///   2. `clearViewPressState` sends the release the nested loop swallowed, so
///      the view's own press bookkeeping (state, pressedIndex, autoscroll) ends
///      where the gesture ended instead of surviving into the next one. It is
///      addressed OUTSIDE any item deliberately: `mouseReleaseEvent` only emits
///      clicked()/activated() when the release lands on the pressed index, and
///      a drag is not a click.
namespace singledrag {

/// Step 1. Call after every setDragDropMode()/setDragEnabled() on a view whose
/// drags are started by an event filter.
inline void disarmViewDrag(QAbstractItemView *view)
{
    if (!view) return;
    // Order matters: setDragDropMode(DragDrop|DragOnly|InternalMove) sets
    // dragEnabled as a side effect, so this has to come last.
    view->setDragEnabled(false);
}

/// Step 2. Call immediately after QDrag::exec() returns.
inline void clearViewPressState(QAbstractItemView *view)
{
    if (!view || !view->viewport()) return;
    const QPointF outside(-1, -1);      // indexAt() -> invalid -> no click signals
    QMouseEvent release(QEvent::MouseButtonRelease, outside, view->viewport()->mapToGlobal(outside),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &release);
}

}   // namespace singledrag

#endif // SINGLEDRAGOWNER_H
