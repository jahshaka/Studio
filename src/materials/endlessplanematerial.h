/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ENDLESSPLANEMATERIAL_H
#define ENDLESSPLANEMATERIAL_H

//ENDLESS PLANE
class EndlessPlaneEffect:public Qt3DRender::QEffect
{
public:
    EndlessPlaneEffect();
    Qt3DRender::QTechnique *tech;
};

class EndlessPlaneMaterial:public Qt3DRender::QMaterial
{
    Qt3DRender::QAbstractTexture* texture;
    Qt3DRender::QParameter* texParam;

    Qt3DRender::QParameter* textureScaleParam;
public:
    EndlessPlaneMaterial();

    void setTexture(Qt3DRender::QAbstractTextureImage* tex);
    void setTexture(Qt3DRender::QAbstractTexture* tex);
    void setTextureScale(float scale);
};

#endif // ENDLESSPLANEMATERIAL_H
