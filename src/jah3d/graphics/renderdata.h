#ifndef RENDERDATA_H
#define RENDERDATA_H

//#include "../core/scene.h"
//#include "../core/scenenode.h"
//#include "material.h"
#include <QSharedPointer>

namespace jah3d
{
class Scene;
class Material;

struct RenderData
{
    QSharedPointer<Scene> scene;
    QSharedPointer<Material> material;

    QMatrix4x4 viewMatrix;
    QMatrix4x4 projMatrix;

    //QList<LightNodePtr> lights;
};

}

#endif // RENDERDATA_H
