/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "scene.h"
#include "scenenode.h"
#include "../scenegraph/lightnode.h"
#include "../scenegraph/cameranode.h"
#include "../graphics/mesh.h"
#include "../materials/defaultskymaterial.h"

namespace iris
{

Scene::Scene()
{
    rootNode = SceneNode::create();
    rootNode->setName("Scene");
    //rootNode->setScene(this->sharedFromThis());

    //todo: move this to ui code
    skyMesh = Mesh::loadMesh("app/content/primitives/sky.obj");
    //skyTexture = Texture2D::load("app/content/skies/default.png");
    skyMaterial = DefaultSkyMaterial::create();
    skyColor = QColor(255,255,255,255);

    fogColor = QColor(250,250,250);
    fogStart = 100;
    fogEnd = 180;
    fogEnabled = true;
}

void Scene::setSkyTexture(Texture2DPtr tex)
{
    skyTexture = tex;
    skyMaterial->setSkyTexture(tex);
}

QString Scene::getSkyTextureSource()
{
    return skyTexture->getSource();
}

void Scene::clearSkyTexture()
{
    skyTexture.clear();
    skyMaterial->clearSkyTexture();
}

void Scene::setSkyColor(QColor color)
{
    this->skyColor = color;
    skyMaterial->setSkyColor(color);
}

void Scene::update(float dt)
{
    rootNode->update(dt);

    //cameras may not necessarily be a part of the scene heirarchy, so their matrices are updated here
    camera->update(dt);
    camera->updateCameraMatrices();
}

void Scene::render()
{

}

void Scene::addNode(SceneNodePtr node)
{
    if(node->sceneNodeType == SceneNodeType::Light)
    {
        auto light = node.staticCast<iris::LightNode>();
        lights.append(light);
    }
}

void Scene::removeNode(SceneNodePtr node)
{
    if(node->sceneNodeType == SceneNodeType::Light)
    {
        lights.removeOne(node.staticCast<iris::LightNode>());
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
