#ifndef SCENE_H
#define SCENE_H

#include <QList>
#include <QSharedPointer>


namespace jah3d
{

class LightNode;
class SceneNode;

typedef QSharedPointer<LightNode> LightNodePtr;
typedef QSharedPointer<SceneNode> SceneNodePtr;
typedef QSharedPointer<Scene> ScenePtr;

class Scene
{
private:
    QList<LightNodePtr> lights;
    SceneNodePtr rootNode;

    Scene()
    {

    }
public:
    ScenePtr create();

};

}


#endif // SCENE_H
