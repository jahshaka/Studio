/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef BILLBOARDMATERIAL_H
#define BILLBOARDMATERIAL_H

#include <QColor>

//BILLBOARD MATERIAL
class BillboardEffect:public Qt3DRender::QEffect
{
public:
    BillboardEffect();

    Qt3DRender::QTechnique *tech;
};

class BillboardMaterial:public Qt3DRender::QMaterial
{
    Qt3DRender::QAbstractTexture* texture;
    Qt3DRender::QParameter* texParam;
    Qt3DRender::QParameter* useTexParam;
    Qt3DRender::QParameter* colorParam;
public:
    BillboardMaterial();
    void setTexture(Qt3DRender::QAbstractTextureImage* tex);
    void setTexture(Qt3DRender::QAbstractTexture* tex);
    void setUseTexture(bool useTexture);
    void setColor(QColor color);
};

#endif // BILLBOARDMATERIAL_H
