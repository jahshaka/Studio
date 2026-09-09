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
#include <QString>

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

    /// True while a script run is in progress (whether or not the run has
    /// recorded anything yet — see beginScriptMacro).
    bool isScriptMacroOpen() const { return mScriptMacroOpen; }
    void setScriptMacroOpen(bool open) { mScriptMacroOpen = open; }

    // ---- the script run's ONE undo entry, opened LAZILY ---------------------
    //
    // A script run is one undo step. It used to be one QUndoStack MACRO opened
    // unconditionally at the start of the run — and QUndoStack keeps an EMPTY
    // macro on the stack, so every query (describe the scene, read a property,
    // any MCP tool call at all) pushed a do-nothing entry that ate the user's
    // next Ctrl+Z. Deferring beginMacro to the first command that actually
    // lands is the fix: a run that records nothing leaves the stack untouched,
    // a run that records anything is still exactly one entry.
    //
    // This is the right place for it because EVERY undo command in the app is
    // pushed through this service — nothing else calls QUndoStack::push.

    /// Arms the run's macro. Nothing reaches the stack until the first push.
    void beginScriptMacro(const QString &text);
    /// Opens the armed macro NOW. For callers that are about to open a nested
    /// macro of their own (editor.beginBatch), which must sit inside the run's
    /// entry, not the other way round.
    void ensureScriptMacroOpen();
    /// Closes the macro if it ever opened. True when the run left an entry.
    bool endScriptMacro();

    /// Saved-state bookkeeping (was MainWindow::undoStackCount). Preserves a
    /// pre-extraction quirk: the old code stored getUndoStackCount(), whose
    /// return type was bool — the "count at last save" was only ever 0 or 1,
    /// and the unsaved-changes prompt compared against the same coercion.
    void markSaved();
    void resetSavedCount() { mSavedCount = 0; }
    bool savedCountMatchesCurrent() const;

    /// "What was LOADED is not what should be SAVED."
    ///
    /// The scene reader can heal a stored scene on the way in (a texture slot
    /// naming the model a texture came in with instead of the texture — the
    /// GLB texture-loss defect, io/scenereader.cpp). The document in memory is
    /// then correct and the one on disk is not, with no undo command anywhere
    /// to say so: the whole dirty story here is the undo stack, and a repaired
    /// load pushes nothing. This flag is that missing signal — it makes the
    /// project read dirty until the next save, so the close prompt offers to
    /// write the corrected scene instead of discarding the repair every time.
    /// Cleared by markSaved(), exactly like the saved count.
    void markContentRepaired() { mContentRepaired = true; }
    bool contentRepaired() const { return mContentRepaired; }

private:
    QUndoStack *mStack = nullptr;
    StudioServices *mServices = nullptr;
    bool mScriptMacroOpen = false;
    /// The lazy script macro: armed by beginScriptMacro, opened by the first
    /// push (or ensureScriptMacroOpen), closed by endScriptMacro.
    bool mMacroArmed = false;
    bool mMacroOpen = false;
    QString mMacroText;
    quint64 mPushCount = 0;
    int  mSavedCount = 0;
    bool mContentRepaired = false;
};

#endif // UNDOSERVICE_H
