#include "scene.h"

namespace jah3d
{

Scene::Scene()
{
    rootNode = SceneNode::create();
}

void Scene::update(float dt)
{

}

void Scene::render()
{

}

void Scene::addNode(SceneNodePtr node)
{
    if(node->sceneNodeType == SceneNodeType::Light)
    {
        lights.append(node.staticCast<LightNode>());
    }
}

void Scene::removeNode(SceneNodePtr node)
{
    if(node->sceneNodeType == SceneNodeType::Light)
    {
        lights.removeOne(node);
    }
}



}
