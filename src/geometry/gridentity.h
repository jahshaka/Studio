/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GRIDENTITY_H
#define GRIDENTITY_H
#include "QEntity"

class GridRenderer;

class GridEntity:public Qt3DCore::QEntity
{
public:
    GridEntity(Qt3DCore::QEntity* parent);
};

#endif // GRIDENTITY_H
