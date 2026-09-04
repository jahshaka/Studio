/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/nodeeditcommand.h"

NodeEditCommand::NodeEditCommand(const QString &text, std::function<void()> redoFn,
                                 std::function<void()> undoFn)
    : mRedo(std::move(redoFn)), mUndo(std::move(undoFn))
{
    setText(text);
}

void NodeEditCommand::undo()
{
    if (mUndo) mUndo();
}

void NodeEditCommand::redo()
{
    if (mRedo) mRedo();
}
