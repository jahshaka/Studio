/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef EDITORDATA_H
#define EDITORDATA_H

#include "../irisgl/src/irisglfwd.h"

class EditorData
{
public:
    iris::CameraNodePtr editorCamera;
    float distFromPivot;
};

#endif // EDITORDATA_H
