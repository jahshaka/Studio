#include "scene.h"
#include "scenenode.h"
#include "../scenegraph/lightnode.h"

namespace jah3d
{

Scene::Scene()
{
    rootNode = SceneNode::create();
    //rootNode->setScene(this->sharedFromThis());
}

void Scene::update(float dt)
{
    rootNode->update(dt);
}

void Scene::render()
{

}

void Scene::addNode(SceneNodePtr node)
{
    if(node->sceneNodeType == SceneNodeType::Light)
    {
        auto light = node.staticCast<jah3d::LightNode>();
        lights.append(light);
    }
}

void Scene::removeNode(SceneNodePtr node)
{
    if(node->sceneNodeType == SceneNodeType::Light)
    {
        lights.removeOne(node.staticCast<jah3d::LightNode>());
    }
}

void Scene::setCamera(CameraNodePtr cameraNode)
{
    camera = cameraNode;
}

ScenePtr Scene::create()
{
    ScenePtr scene(new Scene());
    scene->rootNode->setScene(scene);

    return scene;
}

}
