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
#include "../scenegraph/viewernode.h"
#include "../scenegraph/meshnode.h"
#include "../graphics/mesh.h"
#include "../graphics/renderitem.h"
#include "../materials/defaultskymaterial.h"
#include "../geometry/trimesh.h"
#include "irisutils.h"

namespace iris
{

Scene::Scene()
{
    rootNode = SceneNode::create();
    rootNode->setName("World");
    // rootNode->setScene(this->sharedFromThis());

    // todo: move this to ui code
    skyMesh = Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/sky.obj"));

//    QString x1 = IrisUtils::getAbsoluteAssetPath("app/content/textures/left.jpg");
//    QString x2 = IrisUtils::getAbsoluteAssetPath("app/content/textures/right.jpg");
//    QString y1 = IrisUtils::getAbsoluteAssetPath("app/content/textures/top.jpg");
//    QString y2 = IrisUtils::getAbsoluteAssetPath("app/content/textures/bottom.jpg");
//    QString z1 = IrisUtils::getAbsoluteAssetPath("app/content/textures/front.jpg");
//    QString z2 = IrisUtils::getAbsoluteAssetPath("app/content/textures/back.jpg");

//    skyTexture = Texture2D::createCubeMap(x1, x2, y1, y2, z1, z2);
    skyMaterial = DefaultSkyMaterial::create();
//    skyMaterial->setSkyTexture(skyTexture);
    skyColor = QColor(255, 255, 255, 255);
    skyRenderItem = new RenderItem();
    skyRenderItem->mesh = skyMesh;
    skyRenderItem->material = skyMaterial;
    skyRenderItem->type = RenderItemType::Mesh;
    skyRenderItem->renderLayer = (int)RenderLayer::Background;

    fogColor = QColor(250, 250, 250);
    fogStart = 100;
    fogEnd = 180;
    fogEnabled = true;

    ambientColor = QColor(64, 64, 64);

    //reserve 1000 items initially
    geometryRenderList.reserve(1000);
    shadowRenderList.reserve(1000);
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

void Scene::setAmbientColor(QColor color)
{
    this->ambientColor = color;
}

void Scene::updateSceneAnimation(float time)
{
    rootNode->updateAnimation(time);
}

void Scene::update(float dt)
{
    rootNode->update(dt);

    // cameras aren't always be a part of the scene hierarchy, so their matrices are updated here
    if (!!camera) {
        camera->update(dt);
        camera->updateCameraMatrices();
    }

    this->geometryRenderList.append(skyRenderItem);
}

void Scene::render()
{

}

void Scene::rayCast(const QVector3D& segStart,
                    const QVector3D& segEnd,
                    QList<PickingResult>& hitList)
{
    rayCast(rootNode, segStart, segEnd, hitList);
}

void Scene::rayCast(const QSharedPointer<iris::SceneNode>& sceneNode,
                    const QVector3D& segStart,
                    const QVector3D& segEnd,
                    QList<iris::PickingResult>& hitList)
{
    if ((sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) &&
         sceneNode->isPickable())
    {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();
        auto mesh = meshNode->getMesh();
        if(mesh != nullptr)
        {
            auto triMesh = meshNode->getMesh()->getTriMesh();

            // transform segment to local space
            auto invTransform = meshNode->globalTransform.inverted();
            auto a = invTransform * segStart;
            auto b = invTransform * segEnd;

            QList<iris::TriangleIntersectionResult> results;
            if (int resultCount = triMesh->getSegmentIntersections(a, b, results)) {
                for (auto triResult : results) {
                    // convert hit to world space
                    auto hitPoint = meshNode->globalTransform * triResult.hitPoint;

                    PickingResult pick;
                    pick.hitNode = sceneNode;
                    pick.hitPoint = hitPoint;
                    pick.distanceFromStartSqrd = (hitPoint - segStart).lengthSquared();

                    hitList.append(pick);
                }
            }
        }
    }

    for (auto child : sceneNode->children) {
        rayCast(child, segStart, segEnd, hitList);
    }
}

void Scene::addNode(SceneNodePtr node)
{
    if (node->sceneNodeType == SceneNodeType::Light) {
        auto light = node.staticCast<iris::LightNode>();
        lights.append(light);
    }

    if(node->sceneNodeType == SceneNodeType::Viewer && vrViewer.isNull())
    {
        vrViewer = node.staticCast<iris::ViewerNode>();
    }
}

void Scene::removeNode(SceneNodePtr node)
{
    if (node->sceneNodeType == SceneNodeType::Light) {
        lights.removeOne(node.staticCast<iris::LightNode>());
    }

    // if this node is the scene's viewer then reset the scene's viewer to null
    if (vrViewer == node.staticCast<iris::ViewerNode>()) {
        vrViewer.reset();
    }

    for (auto& child : node->children) {
        removeNode(child);
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

void Scene::setOutlineWidth(int width)
{
    outlineWidth = width;
}

void Scene::setOutlineColor(QColor color)
{
    outlineColor = color;
}

}
