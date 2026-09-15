/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef BLADEROW_H
#define BLADEROW_H

// THE PANEL'S HANDLE ON A ROW IT PUT IN A BLADE (lane PANEL-LIFETIME-1).
//
// A blade RETIRES a row (AccordianBladeWidget::clearPanel) by taking it out of
// the content pane, hiding it and calling deleteLater(). The row is therefore
// alive for a while and dead afterwards, and WHEN it dies is not the panel's
// decision: it dies at the next turn of the event loop, whenever that is.
//
// A panel that holds a RAW pointer to such a row is holding a pointer whose
// validity depends on what the rest of the application does next — and the
// loop turns in places nobody had in mind when the pointer was stored: the
// threaded scene open runs its install stages one per turn
// (services/sceneopenrunner.h), a script run turns it between every verb (the
// worker + nested loop the scripting bridge uses), the sky panel defers its
// own rebuild by one turn on purpose (skypropertywidget.cpp).
//
// So the handle is this, and the rule it enforces is one sentence: A RETIRED
// ROW READS AS NULL. `get()` is null from the moment the blade retires the row
// — before its destruction, not after it — so a panel that kept a handle
// across a rebuild finds nothing there instead of finding a row that is on its
// way out (or a freed one). The two mistakes it makes impossible are the two
// that happened: writing into a row that has left the panel, and dereferencing
// one the event loop has already destroyed.
//
// It converts to `T *` implicitly, so the call sites (connect, the row
// registry, qobject_cast) read exactly as they did when the member was raw;
// what changes is the DECLARATION. Every site that tests the pointer before
// using it (`if (row) row->…`) keeps its shape and gains liveness — that test
// used to mean "this panel built one", and it now means "…and it is still on
// the panel".

#include <QPointer>
#include <QWidget>

namespace bladerow {

// THE MARK LIVES ON THE ROW, so it survives everything a panel does with its
// own members and can be read from a handle to a row this panel has never
// heard of. A dynamic property and not a set of addresses: an address is
// recycled by the next widget, a property dies with the object carrying it.
inline const char *retiredProperty() { return "jah_rowRetired"; }

/// THE WHOLE ROW GOES, CONTROLS AND ALL. A panel keeps handles on controls
/// INSIDE a row as well as on rows (the material blade's "Reset to <provider>"
/// button lives in a row of its own making), and those die with the row — so
/// one question, "is this widget still part of the panel?", is answerable of
/// either. `children()` and not `findChildren()`: no list is allocated, and
/// this runs per retired row on the selection path.
inline void markRetired(QWidget *row)
{
    if (!row) return;
    row->setProperty(retiredProperty(), true);
    for (QObject *child : row->children())
        if (QWidget *cw = qobject_cast<QWidget *>(child)) markRetired(cw);
}

/// Has this row been retired by its blade? A row that never entered a blade,
/// and a row still on one, answer false.
inline bool isRetired(const QWidget *row)
{
    return row && row->property(retiredProperty()).toBool();
}

}   // namespace bladerow

/// See the file header. `T` is the row's own type (ComboBoxWidget,
/// HFloatSliderWidget, PropertyWidget, …).
template <class T>
class RowPtr
{
public:
    RowPtr() = default;
    RowPtr(T *row) : mRow(row) {}                    // NOLINT: implicit by design
    RowPtr &operator=(T *row) { mRow = row; return *this; }

    /// The row, or null if it has been retired from its blade or destroyed.
    /// (T must be complete HERE — a header that reads a RowPtr inline includes
    /// the row's own header, as it would have to for any other use of it.)
    T *get() const
    {
        T *row = mRow.data();
        return (row && !bladerow::isRetired(row)) ? row : nullptr;
    }
    operator T *() const { return get(); }           // NOLINT: implicit by design
    T *operator->() const { return get(); }
    T &operator*() const { return *get(); }
    /// QPointer's spelling of the same two questions, so a member that was a
    /// QPointer reads the same after the change (and now answers about the
    /// PANEL, not only about the heap).
    T *data() const { return get(); }
    bool isNull() const { return get() == nullptr; }

    /// THE RAW POINTER, LIVE OR NOT. The one legitimate use is asking about a
    /// row the panel has retired (a test, a diagnostic); never for a call.
    T *retiredOrLive() const { return mRow.data(); }

private:
    /// QPointer and not T*: the row may be destroyed by a deleteLater the
    /// panel never sees, and a stale address is the whole defect class.
    QPointer<T> mRow;
};

#endif // BLADEROW_H
