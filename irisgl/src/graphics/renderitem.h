/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef RENDERITEM_H
#define RENDERITEM_H

#include "../irisglfwd.h"
#include "../geometry/boundingsphere.h"
#include <QMatrix4x4>
#include "renderstates.h"

class QOpenGLShaderProgram;

namespace iris
{

class Mesh;

enum class RenderItemType
{
    None,
    Mesh,
    ParticleSystem
};

struct RenderItem
{
    RenderItemType type;
    MaterialPtr material;
    QString guid;
    MeshPtr mesh;

    QMatrix4x4 worldMatrix;
    SceneNodePtr sceneNode;

    QOpenGLShaderProgram* shaderProgram;

    //states
    RenderStates renderStates;

    bool cullable = false;
    bool physicsObject = false;
    BoundingSphere boundingSphere;

    //sort order for render layer
    //used if no material is specified
    int renderLayer;

    RenderItem() {
        type = RenderItemType::None;
        worldMatrix.setToIdentity();
    }

    void reset();
};

}
#endif // RENDERITEM_H
