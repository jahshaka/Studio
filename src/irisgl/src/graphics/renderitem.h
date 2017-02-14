#ifndef RENDERITEM_H
#define RENDERITEM_H

#include "../irisglfwd.h"
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

enum class RenderLayer : int
{
    Background = 1000,
    Opaque = 2000,
    AlphaTested = 3000,
    Transparent = 4000,
    Overlay = 5000
};

struct RenderItem
{
    RenderItemType type;
    MaterialPtr matrial;
    Mesh* mesh;

    QMatrix4x4 worldMatrix;
    SceneNodePtr sceneNode;

    //sort order for render layer
    int renderLayer;

    RenderItem()
    {
        type = None,
        renderLayer = RenderLayer::Opaque;
        worldMatrix.setToIdentity();
    }
};

}
#endif // RENDERITEM_H
