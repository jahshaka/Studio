#ifndef SCENE_H
#define SCENE_H

#include <QList>
#include <QSharedPointer>
#include "scenenode.h"
#include "../scenegraph/cameranode.h"
#include "../scenegraph/meshnode.h"

namespace jah3d
{

class LightNode;
class SceneNode;

typedef QSharedPointer<LightNode> LightNodePtr;
//typedef QSharedPointer<SceneNode> SceneNodePtr;
typedef QSharedPointer<Scene> ScenePtr;

class Scene
{
private:

    //QList<MeshNodePtr> meshes;

public:
    CameraNodePtr camera;
    SceneNodePtr rootNode;
    QList<LightNodePtr> lights;

    Scene()
    {
        rootNode = SceneNode::create();
    }
public:
    static ScenePtr create();

    void update(float dt);
    void render();

    //adds nodes to list of renderable nodes
    //adds
    void addNode(SceneNodePtr node)
    {
        if(node->sceneNodeType == SceneNodeType::Light)
        {
            lights.append(node);
        }
    }

    void removeNode(SceneNodePtr node)
    {
        if(node->sceneNodeType == SceneNodeType::Light)
        {
            lights.removeOne(node);
        }
    }
};

}


#endif // SCENE_H
