/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "scenemanager.h"
#include "scenenode.h"
#include "scenenodes/skyboxnode.h"
#include <Qt3DExtras/QSkyboxEntity>
#include <Qt3DExtras/qforwardrenderer.h>
#include <Qt3DRender/QParameter>
#include <Qt3DRender/QDirectionalLight>
#include "jahrenderer.h"

//rootEntity should be a new entity handed that will become the root of this scene
SceneManager::SceneManager(Qt3DCore::QEntity* rootEntity)
{

    rootEntity->setObjectName("Scene");
    rootNode = new WorldNode(rootEntity);
    rootNode->setSceneManager(this);

    //add blank light to prevent default light qt3d adds when scene isnt loaded
    auto light = new Qt3DRender::QDirectionalLight();
    light->setIntensity(0);
    rootEntity->addComponent(light);

    bgColor = QColor(50,50,50);

    fogColor = QColor(200,200,200,255);
    fogStart = 200;
    fogEnd = 500;
    fogEnabled = true;

    initSky();
}

EndlessPlaneNode* SceneManager::getGround()
{
    auto world = static_cast<WorldNode*>(rootNode);
    return world->getGround();
}

//https://github.com/qtproject/qt3d/blob/5fc4924c0040be90186083c742a954782105e1f3/src/render/defaults/qskyboxentity.cpp
//https://github.com/qtproject/qt3d/blob/5.6/src/render/defaults/qskyboxentity.h
//https://github.com/qtproject/qt3d/blob/5fc4924c0040be90186083c742a954782105e1f3/src/render/texture/qabstracttextureimage.h
void SceneManager::createFakeSky()
{
    Qt3DCore::QEntity* skyParent = new Qt3DCore::QEntity(rootNode->getEntity());
    auto skyTransform = new Qt3DCore::QTransform();
    skyTransform->setScale(10000);
    //skyParent->addComponent(skyTransform);

    Qt3DExtras::QSkyboxEntity* sky = new Qt3DExtras::QSkyboxEntity(skyParent);
    sky->setExtension(".png");
    //sky->setBaseName(QUrl::fromLocalFile(":assets/textures/skies/clear/").toString());
    sky->addComponent(skyTransform);
}

void SceneManager::setSkyBoxTextures(QString path,QString extension)
{
    sky->setExtension(extension);
    sky->setBaseName(QUrl::fromLocalFile(path).toString());
    sky->setParent(rootNode->getEntity());
}

void SceneManager::clearSky()
{
    sky->setParent(static_cast<Qt3DCore::QEntity*>(nullptr));
}

void SceneManager::initSky()
{
    auto skyTransform = new Qt3DCore::QTransform();
    skyTransform->setScale(10000);

    sky = new Qt3DExtras::QSkyboxEntity();
    sky->addComponent(skyTransform);
}

void SceneManager::updateFogParams(bool enabled,QColor color,float start,float end)
{
    rootNode->updateFogParams(enabled,color,start,end);
}

void SceneManager::updateFogParams()
{
    rootNode->updateFogParams(fogEnabled,
                              fogColor,
                              fogStart,
                              fogEnd);
}


float SceneManager::getFogStart()
{
    return fogStart;
}

float SceneManager::getFogEnd()
{
    return fogEnd;
}

QColor SceneManager::getFogColor()
{
    return fogColor;
}

bool SceneManager::getFogEnabled()
{
    return fogEnabled;
}

void SceneManager::setFogStart(float fogStart)
{
    this->fogStart = fogStart;
    updateFogParams();
}

void SceneManager::setFogEnd(float fogEnd)
{
    this->fogEnd = fogEnd;
    updateFogParams();
}

void SceneManager::setFogColor(QColor color)
{
    fogColor = color;
    updateFogParams();
}

void SceneManager::setFogEnabled(bool enabled)
{
    fogEnabled = enabled;
    updateFogParams();
}

void SceneManager::setCamera(Qt3DRender::QCamera* cam)
{
    Q_UNUSED(cam);
}

void SceneManager::setRenderer(JahRenderer* renderer)
{
    this->renderer = renderer;
}

JahRenderer* SceneManager::getRenderer()
{
    return this->renderer;
}

SceneNode* SceneManager::getRootNode()
{
    return rootNode;
}

void SceneManager::setRootNode(SceneNode* root)
{
    rootNode = root;
    root->setSceneManager(this);
}

SceneManager* SceneManager::createDefaultMatrixScene(Qt3DCore::QEntity* rootEntity)
{
    auto scene = new SceneManager(new Qt3DCore::QEntity());
    //auto scene = new SceneManager(rootEntity);
    auto worldNode = static_cast<WorldNode*>(scene->getRootNode());
    worldNode->hideGrid();
    worldNode->getEntity()->setParent(rootEntity);

    //default viewer
    auto cam = new UserCameraNode(new Qt3DCore::QEntity());
    scene->getRootNode()->addChild(cam);
    cam->pos = QVector3D(0,3,0);
    cam->rot.setX(180);
    cam->_updateTransform();


    //ground
    auto plane = EndlessPlaneNode::createEndlessPlane("ground");
    //plane->setTexture(":/assets/textures/defaultgrid.png");
    plane->setTexture("app/content/textures/defaultgrid.png");
    worldNode->addChild(plane);

    //sky
    //worldNode->getSky()->setTexture(":/assets/skies/default.png");
    worldNode->getSky()->setTexture("app/content/skies/default.png");

    //directional light
    auto light = LightNode::createLight("Sun",LightType::Directional);
    light->setColor(QColor::fromRgb(200,200,200));
    light->pos = QVector3D(0,10,0);
    //light->rot.setX(180);
    light->rot.setX(180);
    light->_updateTransform();
    //light->set
    worldNode->addChild(light);

    return scene;
}

SceneManager* SceneManager::createDefaultGridScene(Qt3DCore::QEntity* rootEntity)
{
    auto scene = new SceneManager(rootEntity);
    //auto worldNode = static_cast<WorldNode*>(scene->getRootNode());

    //default viewer
    auto cam = new UserCameraNode(new Qt3DCore::QEntity());
    scene->getRootNode()->addChild(cam);
    cam->pos = QVector3D(0,3,0);
    cam->_updateTransform();

    return scene;
}
