/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "editorvrcontroller.h"
#include "../irisgl/src/graphics/renderitem.h"
#include "../irisgl/src/graphics/material.h"
#include "../irisgl/src/graphics/mesh.h"
#include "../irisgl/src/materials/defaultmaterial.h"
#include "../irisgl/src/vr/vrdevice.h"
#include "../irisgl/src/vr/vrmanager.h"
#include "../irisgl/src/core/scene.h"
#include "../irisgl/src/core/scenenode.h"
#include "../irisgl/src/core/irisutils.h"
#include "../irisgl/src/math/mathhelper.h"
#include "../irisgl/src/scenegraph/cameranode.h"
#include "../core/keyboardstate.h"
#include <QtMath>


EditorVrController::EditorVrController()
{
    //auto cube = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/cube.obj"));
    auto leftHandModel = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_left.obj"));

    auto mat = iris::DefaultMaterial::create();
    mat->setDiffuseTexture(
                iris::Texture2D::load(
                    IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_col.png")));

    leftHandRenderItem = new iris::RenderItem();
    leftHandRenderItem->type = iris::RenderItemType::Mesh;
    leftHandRenderItem->material = mat;
    leftHandRenderItem->mesh = leftHandModel;

    auto rightHandModel = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/models/external_controller01_right.obj"));
    rightHandRenderItem = new iris::RenderItem();
    rightHandRenderItem->type = iris::RenderItemType::Mesh;
    rightHandRenderItem->material = mat;
    rightHandRenderItem->mesh = rightHandModel;

    beamMesh = iris::Mesh::loadMesh(
                IrisUtils::getAbsoluteAssetPath("app/content/models/beam.obj"));
    auto beamMat = iris::DefaultMaterial::create();
    beamMat->setDiffuseColor(QColor(255,100,100));

    leftBeamRenderItem = new iris::RenderItem();
    leftBeamRenderItem->type = iris::RenderItemType::Mesh;
    leftBeamRenderItem->material = beamMat;
    leftBeamRenderItem->mesh = beamMesh;

    rightBeamRenderItem = new iris::RenderItem();
    rightBeamRenderItem->type = iris::RenderItemType::Mesh;
    rightBeamRenderItem->material = beamMat;
    rightBeamRenderItem->mesh = beamMesh;
}

void EditorVrController::setScene(iris::ScenePtr scene)
{
    this->scene = scene;
}

void EditorVrController::update(float dt)
{
    vrDevice = iris::VrManager::getDefaultDevice();
    const float linearSpeed = 10.4f * dt;

    // keyboard movement
    const QVector3D upVector(0, 1, 0);
    //not giving proper rotation when not in debug mode
    //apparently i need to normalize the head rotation quaternion
    auto rot = vrDevice->getHeadRotation();
    rot.normalize();
    auto forwardVector = rot.rotatedVector(QVector3D(0, 0, -1));
    auto x = QVector3D::crossProduct(forwardVector,upVector).normalized();
    auto z = QVector3D::crossProduct(upVector,x).normalized();

    // left
    if(KeyboardState::isKeyDown(Qt::Key_Left) ||KeyboardState::isKeyDown(Qt::Key_A) )
        camera->pos -= x * linearSpeed;

    // right
    if(KeyboardState::isKeyDown(Qt::Key_Right) ||KeyboardState::isKeyDown(Qt::Key_D) )
        camera->pos += x * linearSpeed;

    // up
    if(KeyboardState::isKeyDown(Qt::Key_Up) ||KeyboardState::isKeyDown(Qt::Key_W) )
        camera->pos += z * linearSpeed;

    // down
    if(KeyboardState::isKeyDown(Qt::Key_Down) ||KeyboardState::isKeyDown(Qt::Key_S) )
        camera->pos -= z * linearSpeed;

    // touch controls
    auto leftTouch = vrDevice->getTouchController(0);
    if (leftTouch->isTracking()) {
        auto dir = leftTouch->GetThumbstick();
        camera->pos += x * linearSpeed * dir.x() * 2;
        camera->pos += z * linearSpeed * dir.y() * 2;


        if(leftTouch->isButtonDown(iris::VrTouchInput::Y))
            camera->pos += QVector3D(0, linearSpeed, 0);
        if(leftTouch->isButtonDown(iris::VrTouchInput::X))
            camera->pos += QVector3D(0, -linearSpeed, 0);

        camera->rot = QQuaternion();
        camera->update(0);


        // Submit items to renderer
        auto device = iris::VrManager::getDefaultDevice();

        QMatrix4x4 world;
        world.setToIdentity();
        world.translate(device->getHandPosition(0));
        world.rotate(device->getHandRotation(0));
        //world.scale(0.55f);
        leftHandRenderItem->worldMatrix = camera->globalTransform * world;
        leftBeamRenderItem->worldMatrix = leftHandRenderItem->worldMatrix;


        world.setToIdentity();
        world.translate(device->getHandPosition(1));
        world.rotate(device->getHandRotation(1));
        //world.scale(0.55f);
        rightHandRenderItem->worldMatrix = camera->globalTransform * world;
        rightBeamRenderItem->worldMatrix = rightHandRenderItem->worldMatrix;


        // Handle picking and movement of picked objects
        iris::PickingResult pick;
        if (rayCastToScene(leftHandRenderItem->worldMatrix, pick)) {
            auto dist = qSqrt(pick.distanceFromStartSqrd);
            //qDebug() << "hit at dist: " << dist;
            leftBeamRenderItem->worldMatrix.scale(1, 1, dist /* * (1.0f / 0.55f )*/);// todo: remove magic 0.55

            // Pick a node if the trigger is down
            if (leftTouch->getIndexTrigger() > 0.1f && !leftPickedNode)
            {
                leftPickedNode = pick.hitNode;

                //calculate offset
                //leftNodeOffset = leftPickedNode->getGlobalTransform() * leftHandRenderItem->worldMatrix.inverted();
                leftNodeOffset =  leftHandRenderItem->worldMatrix.inverted() * leftPickedNode->getGlobalTransform();
            }

        }
        else
        {
            leftBeamRenderItem->worldMatrix.scale(1, 1, 100.f * (1.0f / 0.55f ));
        }

        if(leftTouch->getIndexTrigger() < 0.1f && !!leftPickedNode)
        {
            // release node
            leftPickedNode.clear();
            leftNodeOffset.setToIdentity(); // why bother?
        }

        // update picked node
        if (!!leftPickedNode) {
            // calculate the global position
            auto nodeGlobal = leftHandRenderItem->worldMatrix * leftNodeOffset;

            // calculate position relative to parent
            auto localTransform = leftPickedNode->parent->getGlobalTransform().inverted() * nodeGlobal;

            // decompose matrix to assign pos, rot and scale
            iris::MathHelper::decomposeMatrix(localTransform,
                                              leftPickedNode->pos,
                                              leftPickedNode->rot,
                                              leftPickedNode->scale);
            leftPickedNode->rot.normalize();

            // @todo: force recalculatioin of global transform
            // leftPickedNode->update(0);// bad!
            // it wil be updated a frame later, no need to stress over this
        }

        if (rayCastToScene(rightBeamRenderItem->worldMatrix, pick)) {
            auto dist = qSqrt(pick.distanceFromStartSqrd);
            rightBeamRenderItem->worldMatrix.scale(1, 1,(dist /*  * (1.0f / 0.55f )*/));// todo: remove magic 0.55
        }

        scene->geometryRenderList.append(leftHandRenderItem);
        scene->geometryRenderList.append(rightHandRenderItem);
    }

    auto rightTouch = vrDevice->getTouchController(1);
    if (rightTouch->isTracking()) {
        scene->geometryRenderList.append(leftBeamRenderItem);
        scene->geometryRenderList.append(rightBeamRenderItem);
    }
}

bool EditorVrController::rayCastToScene(QMatrix4x4 handMatrix, iris::PickingResult& result)
{
    QList<iris::PickingResult> hits;

    scene->rayCast(handMatrix * QVector3D(0,0,0),
                   handMatrix * QVector3D(0,0,-100),
                   hits);

    if(hits.size() == 0)
        return false;

    qSort(hits.begin(), hits.end(), [](const iris::PickingResult& a, const iris::PickingResult& b){
        return a.distanceFromStartSqrd < b.distanceFromStartSqrd;
    });

    result = hits.first();
    return true;
}


