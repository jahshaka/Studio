#ifndef RENDERDATA_H
#define RENDERDATA_H

#include "../core/scene.h"
#include "../core/scenenode.h"
#include "material.h"

namespace jah3d
{

struct RenderData
{
    ScenePtr scene;
    MaterialPtr material;

    QMatrix4x4 viewMatrix;
    QMatrix4x4 projMatrix;

    QList<LightNodePtr> lights;
};

}

#endif // RENDERDATA_H
