/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SKYBOXNODE_H
#define SKYBOXNODE_H

#include <QString>
#include "../scenenode.h"

class SceneNode;

namespace Qt3DRender
{
    class QMaterial;
    class QEffect;
    class QTechnique;
    class QTextureImage;
    class QTexture2D;
    class QParameter;
}

namespace Qt3DCore
{
    class QEntity;
    class QTransform;
}

namespace Qt3DExtras
{
    class QCuboidMesh;
}

class EquiRectSkyMaterial:public Qt3DRender::QMaterial
{
    Qt3DRender::QEffect* effect;
    Qt3DRender::QTechnique *tech;

    Qt3DRender::QTexture2D* texture;
    Qt3DRender::QParameter* texParam;
public:

    EquiRectSkyMaterial();
    void setTexture(QString skyTexture);
    void clearTexture();
};

class SkyBoxNode:public SceneNode
{
    EquiRectSkyMaterial* material;
    Qt3DExtras::QCuboidMesh* cube;
    Qt3DCore::QTransform* transform;
    QString skyTexture;
public:
    SkyBoxNode(Qt3DCore::QEntity* parent);
    void setTexture(QString skyTexture);
    void clearTexture();
    QString getTexture();

    static SkyBoxNode* createSky();
};

#endif // SKYBOXNODE_H
