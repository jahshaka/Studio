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

#include <QUndoStack>

UndoService::UndoService(QUndoStack *stack) : mStack(stack)
{
}

void UndoService::push(QUndoCommand *command)
{
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
    if (!mStack->canUndo()) return;
    mStack->undo();
    if (mStackMoved) mStackMoved();
}

void UndoService::redo()
{
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
