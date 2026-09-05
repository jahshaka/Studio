/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef UNDOSERVICE_H
#define UNDOSERVICE_H

#include <QtGlobal>

// UndoService — the undo spine (APP_ARCHITECTURE_AUDIT §3.3).
//
// Owns the app's QUndoStack policy: pushing commands, the script-macro guard
// (QUndoStack::clear() inside an open macro corrupts the macro accounting, so
// a script run stays one undo step), and the saved-count bookkeeping the
// close-confirmation dialog reads. QObject-free and headless-safe.
//
class QUndoStack;
class QUndoCommand;
struct StudioServices;

class UndoService
{
public:
    /// Does not take ownership; the stack outlives the service (it is parented
    /// to the shell window exactly as before the extraction).
    explicit UndoService(QUndoStack *stack);

    QUndoStack *stack() const { return mStack; }

    /// The aggregate stamped onto every StudioCommand at push time so
    /// commands can raise UI refreshes without ambient statics (Phase 4).
    /// Nullable — headless hosts never set it.
    void setServices(StudioServices *services) { mServices = services; }

    void push(QUndoCommand *command);
    /// Undoes the last completed step if there is one (MainWindow::undo's guard).
    void undo();
    void redo();
    /// Clears the stack — unless a script run's macro is open (the guard that
    /// used to be UiManager::scriptMacroOpen + clearUndoStack).
    void clear();

    bool isDirty() const;
    int  count() const;

    /// How many commands have EVER been pushed through this service.
    ///
    /// Not derivable from the stack: while a macro is open — and a script run
    /// is one, always — QUndoStack::count() does not move, because the pushed
    /// commands become children of the macro. So "did that action record an
    /// undo step?" is unanswerable from the stack inside a script, which is the
    /// only place tests can ask it. Same class of diagnostic (and the same
    /// reason) as SceneMirror's GI push counters. Never falls.
    quint64 pushCount() const { return mPushCount; }

    /// True while a script run's one-undo-step macro is open.
    bool isScriptMacroOpen() const { return mScriptMacroOpen; }
    void setScriptMacroOpen(bool open) { mScriptMacroOpen = open; }

    /// Saved-state bookkeeping (was MainWindow::undoStackCount). Preserves a
    /// pre-extraction quirk: the old code stored getUndoStackCount(), whose
    /// return type was bool — the "count at last save" was only ever 0 or 1,
    /// and the unsaved-changes prompt compared against the same coercion.
    void markSaved();
    void resetSavedCount() { mSavedCount = 0; }
    bool savedCountMatchesCurrent() const;

private:
    QUndoStack *mStack = nullptr;
    StudioServices *mServices = nullptr;
    bool mScriptMacroOpen = false;
    quint64 mPushCount = 0;
    int  mSavedCount = 0;
};

#endif // UNDOSERVICE_H
