/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/changematerialcommand.h"

#include "irisgl/document/scenegraph/meshnode.h"

ChangeMaterialCommand::ChangeMaterialCommand(iris::MeshNodePtr meshNode,
                                             iris::MaterialPtr newMaterial)
    : meshNode(meshNode),
      oldMaterial(meshNode->getMaterial()),
      newMaterial(newMaterial)
{
    setText(QObject::tr("Apply Material"));
}

void ChangeMaterialCommand::redo()
{
    meshNode->setMaterial(newMaterial);
}

void ChangeMaterialCommand::undo()
{
    meshNode->setMaterial(oldMaterial);
}
