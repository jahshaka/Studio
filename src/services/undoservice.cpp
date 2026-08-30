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
    mStack->push(command);
}

void UndoService::undo()
{
    if (mStack->canUndo()) mStack->undo();
}

void UndoService::redo()
{
    if (mStack->canRedo()) mStack->redo();
}

void UndoService::clear()
{
    // Clearing inside an open macro corrupts QUndoStack's macro accounting
    // ("endMacro(): no matching beginMacro()"); a script run stays one undo
    // step instead, which is the scripting contract anyway.
    if (mScriptMacroOpen) return;
    mStack->clear();
}

bool UndoService::isDirty() const
{
    return !mStack->isClean();
}

int UndoService::count() const
{
    return mStack->count();
}

void UndoService::markSaved()
{
    mSavedCount = mStack->count() != 0 ? 1 : 0;
}

bool UndoService::savedCountMatchesCurrent() const
{
    return mSavedCount == (mStack->count() != 0 ? 1 : 0);
}
