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
#include "../scenegraph/particlesystemnode.h"
#include "../graphics/mesh.h"
#include "../graphics/renderitem.h"
#include "../materials/defaultskymaterial.h"
#include "../geometry/trimesh.h"
#include "../core/irisutils.h"
#include "../graphics/renderlist.h"

#include "physics/environment.h"
#include "math/intersectionhelper.h"

namespace iris
{

Scene::Scene()
{
    rootNode = SceneNode::create();
    rootNode->setName("World");
    // rootNode->setScene(this->sharedFromThis());

    // todo: move this to ui code
    skyMesh = Mesh::loadMesh(":assets/models/sky.obj");

//    QString x1 = IrisUtils::getAbsoluteAssetPath("app/content/textures/left.jpg");
//    QString x2 = IrisUtils::getAbsoluteAssetPath("app/content/textures/right.jpg");
//    QString y1 = IrisUtils::getAbsoluteAssetPath("app/content/textures/top.jpg");
//    QString y2 = IrisUtils::getAbsoluteAssetPath("app/content/textures/bottom.jpg");
//    QString z1 = IrisUtils::getAbsoluteAssetPath("app/content/textures/front.jpg");
//    QString z2 = IrisUtils::getAbsoluteAssetPath("app/content/textures/back.jpg");

    clearColor = QColor(0,0,0,0);
    renderSky = true;
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

    meshes.reserve(100);
    particleSystems.reserve(100);

    geometryRenderList = new RenderList();
    shadowRenderList = new RenderList();
    gizmoRenderList = new RenderList();

	time = 0;

    environment = QSharedPointer<Environment>(new Environment(geometryRenderList));
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
	time += dt < 0 ? 0 : dt;
    rootNode->update(dt);

    // cameras aren't always a part of the scene hierarchy, so their matrices are updated here
    if (!!camera) {
        camera->update(dt);
		camera->updateCameraMatrices();
    }

    // advance simulation
    environment->stepSimulation(dt);

    if (environment->isSimulating() && !environment->hashBodies.isEmpty()) {
        for (const auto &node : rootNode->children) {
        // Override the mesh's transform if it's a physics body
        // Not the end place since we need to transform empties as well
        // Iterate through the entire scene and change physics object transforms as per NN
            if (node->isPhysicsBody) {
                btTransform trans;
                float matrix[16];
                environment->hashBodies.value(node->getGUID())->getMotionState()->getWorldTransform(trans);
                trans.getOpenGLMatrix(matrix);
                node->setGlobalTransform(QMatrix4x4(matrix).transposed());
            }
        }
    }

    // add items to renderlist
    for (const auto &mesh : meshes) {
        mesh->submitRenderItems();
    }

    for (const auto &particle : particleSystems) {
        particle->submitRenderItems();
    }

    if (renderSky) this->geometryRenderList->add(skyRenderItem);
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
            
            // transform segment to local space
            auto invTransform = meshNode->globalTransform.inverted();
            auto a = invTransform * segStart;
            auto b = invTransform * segEnd;

			// ray-sphere intersection first
			auto mesh = meshNode->getMesh();
			auto sphere = mesh->getBoundingSphere();
			float t;
			QVector3D hitPoint;
			if (IntersectionHelper::raySphereIntersects(a, (b - a).normalized(), sphere.pos, sphere.radius, t, hitPoint)) {
				auto triMesh = meshNode->getMesh()->getTriMesh();

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
    }

    for (auto child : sceneNode->children) {
        rayCast(child, segStart, segEnd, hitList);
    }
}

void Scene::addNode(SceneNodePtr node)
{
    if (!!node->scene)
    {
        //qDebug() << "Node already has scene";
        //throw "Node already has scene";
    }

    if (node->sceneNodeType == SceneNodeType::Light) {
        auto light = node.staticCast<iris::LightNode>();
        lights.append(light);
    }

    if (node->sceneNodeType == SceneNodeType::Mesh) {
        auto mesh = node.staticCast<iris::MeshNode>();
        meshes.append(mesh);
    }

    if (node->sceneNodeType == SceneNodeType::ParticleSystem) {
        auto particleSystem = node.staticCast<iris::ParticleSystemNode>();
        particleSystems.append(particleSystem);
    }

    if(node->sceneNodeType == SceneNodeType::Viewer)
    {
        auto viewer = node.staticCast<iris::ViewerNode>();
        viewers.append(viewer);

        if ( vrViewer.isNull())
            vrViewer = viewer;
    }
}

void Scene::removeNode(SceneNodePtr node)
{
    if (node->sceneNodeType == SceneNodeType::Light) {
        lights.removeOne(node.staticCast<iris::LightNode>());
    }

    if (node->sceneNodeType == SceneNodeType::Mesh) {
        meshes.removeOne(node.staticCast<iris::MeshNode>());
    }

    if (node->sceneNodeType == SceneNodeType::ParticleSystem) {
        particleSystems.removeOne(node.staticCast<iris::ParticleSystemNode>());
    }

    if (node->sceneNodeType == SceneNodeType::Viewer) {
        auto viewer = node.staticCast<iris::ViewerNode>();
        viewers.removeOne(viewer);

        // remove viewer and replace it if more viewers are available
        if ( vrViewer == viewer) {
            if(viewers.count()==0)
                vrViewer.reset();
            else
                vrViewer = viewers[viewers.count()-1];
        }
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

void Scene::cleanup()
{
    camera.clear();
    rootNode.clear();
    vrViewer.clear();

    skyMesh.clear();
    skyTexture.clear();
    skyMaterial.clear();
    delete skyRenderItem;

    lights.clear();
    meshes.clear();
    particleSystems.clear();
    viewers.clear();

    delete geometryRenderList;
    delete shadowRenderList;
    delete gizmoRenderList;
}

}
