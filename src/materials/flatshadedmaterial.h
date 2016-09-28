/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#ifndef FLATSHADEDMATERIAL_H
#define FLATSHADEDMATERIAL_H

class FlatShadedEffect:public Qt3DRender::QEffect
{
public:
    FlatShadedEffect();

    Qt3DRender::QTechnique *tech;
};

class FlatShadedMaterial:public Qt3DRender::QMaterial
{
    Qt3DRender::QAbstractTexture* texture;
    Qt3DRender::QParameter* texParam;
public:
    FlatShadedMaterial();
    void setTexture(Qt3DRender::QAbstractTexture* tex);
};

#endif // FLATSHADEDMATERIAL_H
