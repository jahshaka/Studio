#ifndef RENDERITEM_H
#define RENDERITEM_H

#include "../irisglfwd.h"
#include "material.h"
#include <QMatrix4x4>

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
    MaterialPtr matrial;
    Mesh* mesh;

    QMatrix4x4 worldMatrix;
    SceneNodePtr sceneNode;

    //sort order for render layer
    //int renderLayer;

    RenderItem()
    {
        type = RenderItemType::None,
        //renderLayer = (int)RenderLayer::Opaque;
        worldMatrix.setToIdentity();
    }
};

}
#endif // RENDERITEM_H
