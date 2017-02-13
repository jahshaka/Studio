#ifndef VIEWERNODE_H
#define VIEWERNODE_H

#include "../irisglfwd.h"
#include "../core/scenenode.h"

namespace iris
{

class Mesh;
class ViewerMaterial;

class ViewerNode : public SceneNode
{
    float viewScale;
    ViewerNode();

public:
    Mesh* headModel;
    ViewerMaterialPtr material;

    void setViewScale(float scale);
    float getViewScale();

    static ViewerNodePtr create();
};

}

#endif // VIEWERNODE_H
