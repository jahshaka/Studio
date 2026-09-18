/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/undoservice.h"

#include "commands/studiocommand.h"
#include "services/editgate.h"

#include <QUndoStack>

UndoService::UndoService(QUndoStack *stack) : mStack(stack)
{
}

void UndoService::push(QUndoCommand *command)
{
    // THE EDIT GATE, AT THE SPINE (owner, ledger §423; services/editgate.h).
    //
    // Every undo command in the app is pushed through this one function, and a
    // command does its work in the redo() that QUndoStack::push runs — so a
    // hand edit refused HERE never reaches the document at all, whatever
    // widget or menu it came from, including the ones written after this. The
    // few gestures that write the document LIVE and only push when the gesture
    // ends (a slider drag, the gizmo, the transform fields) cannot be refused
    // by a command that never runs, so they ask the same gate at their own
    // start — see editgate.h for why that list is short and where it lives.
    //
    // The run's OWN commands are not refused: the gate is open inside a verb.
    if (editgate::refuse()) {
        delete command;
        return;
    }
    // A BORROWED MATERIAL IS NOT THE ONE AN UNDO STEP MAY CAPTURE
    // (MATERIAL-PREVIEW-1). The hover preview lends a mesh's material slot
    // while a material is dragged over it; a command pushed while it is live
    // would record the BORROWED material as the state to come back to, and an
    // undo would leave the user looking at a material they never applied. Here
    // for the reason the edit gate is: this is the one function every command
    // in the app goes through, including the ones written after this. A HOOK,
    // like the two below it, so this class stays QObject-free and the suites
    // that compile it alone keep linking.
    if (mPrePush) mPrePush();
    // Stamp before mStack->push — QUndoStack runs the command's first redo()
    // inside push(), and the refresh notifications need the services then.
    if (auto studioCommand = dynamic_cast<StudioCommand *>(command))
        studioCommand->setServices(mServices);
    ++mPushCount;
    // The run's undo entry is created HERE, by the first command that lands —
    // never at the start of the run (see beginScriptMacro).
    ensureScriptMacroOpen();
    mStack->push(command);
}

void UndoService::beginScriptMacro(const QString &text)
{
    mMacroText = text;
    mMacroArmed = true;
}

void UndoService::ensureScriptMacroOpen()
{
    if (!mMacroArmed || mMacroOpen) return;
    mMacroOpen = true;
    mStack->beginMacro(mMacroText);
}

bool UndoService::endScriptMacro()
{
    mMacroArmed = false;
    if (!mMacroOpen) return false;
    mMacroOpen = false;
    mStack->endMacro();
    return true;
}

void UndoService::undo()
{
    // THE EDIT GATE, ON THE WAY BACK TOO (round 2, item 2). An undo is a
    // document write like any other, and it is the one hand edit that needs no
    // command of its own to reach the document — so refusing at push() alone
    // left Ctrl+Z, Ctrl+Y and the MCP undo_redo tool moving the scene under a
    // running script. Worse before the run's first write: the macro opens
    // LAZILY, so during a read-only run canUndo() is true and the step it
    // would reach is the user's own. editor.undo/redo arrive inside a verb
    // scope and pass, exactly like every other verb.
    if (editgate::refuse()) return;
    if (!mStack->canUndo()) return;
    mStack->undo();
    if (mStackMoved) mStackMoved();
}

void UndoService::redo()
{
    if (editgate::refuse()) return;         // as in undo() above
    if (!mStack->canRedo()) return;
    mStack->redo();
    if (mStackMoved) mStackMoved();
}

void UndoService::clear()
{
    // Clearing inside an open macro corrupts QUndoStack's macro accounting
    // ("endMacro(): no matching beginMacro()"); a script run stays one undo
    // step instead, which is the scripting contract anyway. The project verbs
    // END the run's entry before closing or switching projects, so a scripted
    // close really does clear (CLOSE-2 item 2).
    if (mMacroArmed) return;
    mStack->clear();
    // Every command that just died appended its asset-row cleanup instead of
    // writing it (CLOSE-1). One transaction for the lot, here, where the
    // clear's cost is already being paid — and where a failure has somewhere
    // to be reported from.
    if (mDeferredFlush) mDeferredFlush();
}

bool UndoService::isDirty() const
{
    // A load-time repair counts as an edit: nothing was pushed, but the
    // in-memory document no longer matches the stored one (markContentRepaired).
    return !mStack->isClean() || mContentRepaired;
}

int UndoService::count() const
{
    return mStack->count();
}

void UndoService::markSaved()
{
    mSavedCount = mStack->count() != 0 ? 1 : 0;
    mContentRepaired = false;
}

bool UndoService::savedCountMatchesCurrent() const
{
    if (mContentRepaired) return false;
    return mSavedCount == (mStack->count() != 0 ? 1 : 0);
}
