/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef COLORMATERIAL_H
#define COLORMATERIAL_H

class ColorEffect:public Qt3DRender::QEffect
{
public:
    ColorEffect();
    Qt3DRender::QTechnique *tech;
};

class ColorMaterial:public Qt3DRender::QMaterial
{
    Qt3DRender::QParameter* colorParam;
public:
    ColorMaterial();
    void setColor(QColor color);
};

#endif // COLORMATERIAL_H
