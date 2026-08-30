/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef STUDIOCOMMAND_H
#define STUDIOCOMMAND_H

// StudioCommand — base for undo commands that raise UI refreshes.
//
// Phase 4 (hub dissolution): commands used to reach widgets through
// UiManager's statics. Now UndoService::push stamps the services aggregate
// onto every StudioCommand before the stack runs redo(), and the commands
// notify through SceneEditService's signals / SelectionService instead.
// The pointer is nullable — headless tests push commands with no services
// wired, exactly as the old statics were null there.

#include <QUndoCommand>

struct StudioServices;

class StudioCommand : public QUndoCommand
{
public:
    using QUndoCommand::QUndoCommand;

    void setServices(StudioServices *s) { services = s; }

protected:
    StudioServices *services = nullptr;
};

#endif // STUDIOCOMMAND_H
