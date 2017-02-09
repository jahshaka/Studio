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
    ViewerNode();

public:
    Mesh* headModel;
    ViewerMaterialPtr material;

    static ViewerNodePtr create();
};

}

#endif // VIEWERNODE_H
