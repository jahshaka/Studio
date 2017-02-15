#ifndef VIEWERNODE_H
#define VIEWERNODE_H

#include "../irisglfwd.h"
#include "../core/scenenode.h"

namespace iris
{

class RenderItem;
class Mesh;
class ViewerMaterial;

class ViewerNode : public SceneNode
{
    float viewScale;
    ViewerNode();
    ~ViewerNode();

public:
    Mesh* headModel;
    ViewerMaterialPtr material;

    void setViewScale(float scale);
    float getViewScale();

    static ViewerNodePtr create();

    void submitRenderItems() override;

    RenderItem* renderItem;
};

}

#endif // VIEWERNODE_H
