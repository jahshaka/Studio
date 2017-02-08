#ifndef VIEWERNODE_H
#define VIEWERNODE_H

#include "../core/scenenode.h"

namespace iris
{

class Mesh;
class ViewerMaterial;

class ViewerNode : public SceneNode
{
public:
    ViewerNode();

    Mesh* headModel;
    ViewerMaterialPtr material;
};

}

#endif // VIEWERNODE_H
