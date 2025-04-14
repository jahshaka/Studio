/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef DELETESCENENODECOMMAND_H
#define DELETESCENENODECOMMAND_H

#include <QUndoCommand>
#include "irisglfwd.h"

class MainWindow;
class SceneHierarchyWidget;

class DeleteSceneNodeCommand : public QUndoCommand
{
    iris::SceneNodePtr parentNode;
    iris::SceneNodePtr sceneNode;
    int position;
public:
    DeleteSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode);

    void undo() override;
    void redo() override;
};


#endif // DELETESCENENODECOMMAND_H
