/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ADDSCENENODECOMMAND_H
#define ADDSCENENODECOMMAND_H

#include "commands/studiocommand.h"
#include "irisgl/irisglfwd.h"


class AddSceneNodeCommand : public StudioCommand
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
