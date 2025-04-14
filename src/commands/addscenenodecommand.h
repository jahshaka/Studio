/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ADDSCENENODECOMMAND_H
#define ADDSCENENODECOMMAND_H

#include <QUndoCommand>
#include "irisglfwd.h"

class MainWindow;
class SceneHierarchyWidget;

class AddSceneNodeCommand : public QUndoCommand
{
    //iris::ScenePtr scene;
    iris::SceneNodePtr parentNode;
    iris::SceneNodePtr sceneNode;
public:
    AddSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode);

    void undo() override;
    void redo() override;
};

#endif // ADDSCENENODECOMMAND_H
