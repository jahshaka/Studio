#ifndef SCENE_H
#define SCENE_H

#include <QList>
#include <QSharedPointer>
#include "scenenode.h"

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
    QList<LightNodePtr> lights;
    SceneNodePtr rootNode;

    Scene()
    {
        rootNode = SceneNode::create();
    }
public:
    ScenePtr create();

    void update(float dt);
    void render();
};

}


#endif // SCENE_H
