/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GLOBALS_H
#define GLOBALS_H

class Project;
class SceneViewWidget;

class Globals
{
public:
    static float animFrameTime;//maybe this needs to go
    static Project* project;
    static SceneViewWidget* sceneViewWidget;
};

#endif // GLOBALS_H
