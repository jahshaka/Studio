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
    ViewerMaterial* material;
};

}

#endif // VIEWERNODE_H
