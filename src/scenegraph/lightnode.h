/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIGHTNODE_H
#define LIGHTNODE_H

//#include "../scenenode.h"

class SceneNode;
namespace Qt3DCore
{
    class QEntity;
}

namespace Qt3DRender
{
    class QLight;
    class QSphereMesh;
}

enum class LightType
{
    Point,
    SpotLight,
    Directional
};

class LightNode:public SceneNode
{
    LightType lightType;
    Qt3DRender::QLight* light;
    Qt3DRender::* sphere;
public:
    LightNode(Qt3DCore::QEntity* entity);
    LightNode(Qt3DCore::QEntity* entity,LightType type);

    void setLightType(LightType type);

    QColor getColor();
    void setColor(QColor color);
    void setIntensity(float intensity);
    float getIntensity();

    virtual void _updateTransform(bool updateChildren = false) override;

    static LightNode* createPointLight();
    static LightNode* createLight(QString name,LightType type);
};

#endif // LIGHTNODE_H
