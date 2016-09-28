/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "gridentity.h"
#include "gridrenderer.h"
#include "../materials/materials.h"

GridEntity::GridEntity(Qt3DCore::QEntity* parent):
    Qt3DCore::QEntity(parent)
{
    this->addComponent(new GridRenderer());
    auto mat = new ColorMaterial();
    mat->setColor(QColor::fromRgb(255,255,255));
    this->addComponent(mat);
}
