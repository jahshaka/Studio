/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "globals.h"
#include "core/project.h"
#include "widgets/sceneviewwidget.h"

float Globals::animFrameTime = 0;
Project* Globals::project = Project::createNew();
SceneViewWidget* Globals::sceneViewWidget = nullptr;
