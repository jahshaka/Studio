/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "playermousecontroller.h"
#include "../editor/animationpath.h"
#include "../core/keyboardstate.h"
#include "../widgets/sceneviewwidget.h"
#include <irisgl/SceneGraph.h>
#include <irisgl/Vr.h>
#include <irisgl/Physics.h>
#include <irisgl/Graphics.h>
#include "irisgl/src/geometry/trimesh.h"


void PlayerMouseController::setViewer(const iris::ViewerNodePtr &value)
{
    viewer = value;
}

void PlayerMouseController::start()
{
	// reset viewer since a new one could be added since scene
	// was set and playing
	this->setViewer(scene->getActiveVrViewer());

    if (!!viewer) {
		viewer->hide();

		// plant viewer to any surface below it
		auto rayStart = viewer->getGlobalPosition();
		auto rayEnd = rayStart + QVector3D(0, -1000, 0);
		QList<iris::PickingResult> results;
		scene->rayCast(rayStart, rayEnd, results, 0, true);

		// closest point
		QVector3D closestPoint = rayEnd;
		float closestDist = 1000;
		if (results.size() > 0) {
			// find closest one
			for (const auto result : results) {
				auto dist = result.hitPoint.distanceToPoint(closestPoint);
				if (dist < closestDist)
				{
					closestDist = dist;
					closestPoint = result.hitPoint;
				}
			}

			// todo: should limit snapping distance?
			if (closestDist < 1000) {
				viewer->setLocalPos(closestPoint + QVector3D(0, 5.75f * 0.5f, 0));
			}
			scene->getPhysicsEnvironment()->removeCharacterControllerFromWorld(viewer->getGUID());
			scene->getPhysicsEnvironment()->addCharacterControllerToWorldUsingNode(viewer);

			// set rot to viewer's default transform
			camera->setLocalTransform(viewer->getGlobalTransform());

		}
		else {
			// set rot to viewer's default transform
			camera->setLocalTransform(viewer->getGlobalTransform());
		}
        
    }

    // capture cam transform
    camPos = camera->getLocalPos();
    camRot = camera->getLocalRot();

	// capture yaw and pitch
	float roll;
	camera->getLocalRot().getEulerAngles(&pitch, &yaw, &roll);
}

void PlayerMouseController::end()
{
    //clearViewer();
	if (!!viewer)
		viewer->show();

	if (shouldRestoreCameraTransform) {
		// restore cam transform
		camera->setLocalPos(camPos);
		camera->setLocalRot(camRot);
	}
	camera->update(0);
}

void PlayerMouseController::clearViewer()
{
    viewer->show();
    viewer.clear();
}

void PlayerMouseController::setRestoreCameraTransform(bool shouldRestore)
{
	this->shouldRestoreCameraTransform = shouldRestore;
}

void PlayerMouseController::onMouseMove(int dx, int dy)
{
    if (!!pickedNode && _isPlaying) {
        // update picking constraint
        if (leftMouseDown && !!pickedNode) {
            if (pickedNode->isPhysicsBody) {
                scene->getPhysicsEnvironment()->updatePickingConstraint(iris::PickingHandleType::MouseButton, iris::PhysicsHelper::btVector3FromQVector3D(calculateMouseRay(QPointF(mouseX, mouseY)) * 1024),
                                                                        iris::PhysicsHelper::btVector3FromQVector3D(this->camera->getGlobalPosition()));
            }
        }
    } else {
        if(rightMouseDown) {
            this->yaw += dx/10.0f;
            this->pitch += dy/10.0f;
        }
    }
    updateCameraTransform();
}

QVector3D PlayerMouseController::calculateMouseRay(const QPointF& pos)
{
    float x = pos.x();
    float y = pos.y();

    // viewport -> NDC
    float mousex = (2.0f * x) / this->viewport.width - 1.0f;
    float mousey = (2.0f * y) / this->viewport.height - 1.0f;
    QVector2D NDC = QVector2D(mousex, -mousey);

    // NDC -> HCC
    QVector4D HCC = QVector4D(NDC, -1.0f, 1.0f);

    // HCC -> View Space
    QMatrix4x4 projection_matrix_inverse = this->camera->projMatrix.inverted();
    QVector4D eye_coords = projection_matrix_inverse * HCC;
    QVector4D ray_eye = QVector4D(eye_coords.x(), eye_coords.y(), -1.0f, 0.0f);

    // View Space -> World Space
    QMatrix4x4 view_matrix_inverse = this->camera->viewMatrix.inverted();
    QVector4D world_coords = view_matrix_inverse * ray_eye;
    QVector3D final_ray_coords = QVector3D(world_coords);

    return final_ray_coords.normalized();
}

void PlayerMouseController::onMouseWheel(int delta)
{

}

void PlayerMouseController::onMouseDown(Qt::MouseButton button)
{
    CameraControllerBase::onMouseDown(button);
    if (button == Qt::LeftButton && _isPlaying) {
        this->doObjectPicking(QPointF(this->mouseX, this->mouseY));
    }
}

void PlayerMouseController::onMouseUp(Qt::MouseButton button)
{
    CameraControllerBase::onMouseUp(button);
    if (button == Qt::LeftButton && _isPlaying) {
        //scene->getPhysicsEnvironment()->removeConstraintFromWorld()
        this->pickedNode.clear();
        scene->getPhysicsEnvironment()->cleanupPickingConstraint(iris::PickingHandleType::MouseButton);
    }
}

void PlayerMouseController::doObjectPicking(
    const QPointF& point)
{
    camera->updateCameraMatrices();

    auto segStart = screenSpaceToWoldSpace(point, -1.0f);
    auto segEnd = screenSpaceToWoldSpace(point, 1.0f);

    QList<PickingResult> hitList;
    doScenePicking(scene->getRootNode(), segStart, segEnd, hitList);

    // sort by distance to camera then return the closest hit node
    std::sort(hitList.begin(), hitList.end(), [](const PickingResult& a, const PickingResult& b) {
        return a.distanceFromCameraSqrd > b.distanceFromCameraSqrd;
    });

    if (hitList.size() == 0) {
        this->pickedNode.clear();
        return;
    }
    auto pickedNode = hitList.last().hitNode;
    iris::SceneNodePtr lastSelectedRoot;

    auto pickedRoot = hitList.last().hitNode;
    while (pickedRoot->isAttached())
        pickedRoot = pickedRoot->parent;

    if (pickedNode->isPhysicsBody) {
        scene->getPhysicsEnvironment()->createPickingConstraint(iris::PickingHandleType::MouseButton,
                                                                pickedNode->getGUID(),
                                                                iris::PhysicsHelper::btVector3FromQVector3D(hitList.last().hitPoint),
                                                                segStart,
                                                                segEnd);
        this->pickedNode = pickedNode;
    }
}

void PlayerMouseController::doScenePicking(const QSharedPointer<iris::SceneNode>& sceneNode,
                                     const QVector3D& segStart,
                                     const QVector3D& segEnd,
                                     QList<PickingResult>& hitList)
{
    if ((sceneNode->getSceneNodeType() == iris::SceneNodeType::Mesh) &&
         sceneNode->isPickable())
    {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();
        auto mesh = meshNode->getMesh();
        if (mesh != nullptr) {
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
                    pick.distanceFromCameraSqrd = (hitPoint - camera->getGlobalPosition()).lengthSquared();

                    hitList.append(pick);
                }
            }
        }
    }

    for (auto child : sceneNode->children) {
        doScenePicking(child, segStart, segEnd, hitList);
    }
}

QVector3D PlayerMouseController::screenSpaceToWoldSpace(const QPointF& pos, float depth)
{
    float x = pos.x();
    float y = pos.y();
    // viewport -> NDC
    float mousex = (2.0f * x) / this->viewport.width - 1.0f;
    float mousey = (2.0f * y) / this->viewport.height - 1.0f;
    QVector2D NDC = QVector2D(mousex, -mousey);

    // NDC -> HCC
    QVector4D HCC = QVector4D(NDC, depth, 1.0f);

    // HCC -> View Space
    QMatrix4x4 projection_matrix_inverse = this->camera->projMatrix.inverted();
    QVector4D eye_coords = projection_matrix_inverse * HCC;
    //QVector4D ray_eye = QVector4D(eye_coords.x(), eye_coords.y(), eye_coords.z(), 0.0f);

    // View Space -> World Space
    QMatrix4x4 view_matrix_inverse = this->camera->viewMatrix.inverted();
    QVector4D world_coords = view_matrix_inverse * eye_coords;


    return world_coords.toVector3D() / world_coords.w();
}

void PlayerMouseController::setViewport(const iris::Viewport &viewport)
{
    this->viewport = viewport;
}

void PlayerMouseController::updateCameraTransform()
{
    if (!!viewer && _isPlaying) {
		camera->setLocalPos(viewer->getGlobalPosition());
		auto viewMat = viewer->getGlobalTransform().normalMatrix();
		QQuaternion rot = QQuaternion::fromRotationMatrix(viewMat);
		camera->setLocalRot(rot * QQuaternion::fromEulerAngles(pitch, yaw, 0));
		//camera->setLocalRot(QQuaternion::fromEulerAngles(pitch,yaw,0));
	}
	else {
		camera->setLocalRot(QQuaternion::fromEulerAngles(pitch, yaw, 0));
	}
    camera->update(0);
}

void PlayerMouseController::captureYawPitchRollFromCamera()
{
	// capture yaw and pitch
	float roll; // roll not used
	camera->getLocalRot().getEulerAngles(&pitch, &yaw, &roll);
}

PlayerMouseController::PlayerMouseController()
{
    yaw = 0;
    pitch = 0;
    movementSpeed = 25;
}

void PlayerMouseController::setCamera(iris::CameraNodePtr cam)
{
	camera = cam;
}

void PlayerMouseController::setScene(iris::ScenePtr scene)
{
	this->scene = scene;
	this->setViewer(scene->getActiveVrViewer());
}

void PlayerMouseController::update(float dt)
{
    auto linearSpeed = 15 * dt;
    if (!_isPlaying) {
        this->doGodMode(dt);
		return;
    }
	const QVector3D upVector(0, 1, 0);
	auto forwardVector = camera->getLocalRot().rotatedVector(QVector3D(0, 0, -1));
	auto x = QVector3D::crossProduct(forwardVector, upVector).normalized();
	auto z = QVector3D::crossProduct(upVector, x).normalized();

	if (!viewer) {
		auto camPos = camera->getLocalPos();
		// left
		if (KeyboardState::isKeyDown(Qt::Key_Left))
			camPos -= x * linearSpeed;

		// right
		if (KeyboardState::isKeyDown(Qt::Key_Right))
			camPos += x * linearSpeed;

		// up
		if (KeyboardState::isKeyDown(Qt::Key_Up))
			camPos += z * linearSpeed;

		// down
		if (KeyboardState::isKeyDown(Qt::Key_Down))
			camPos -= z * linearSpeed;

		camera->setLocalPos(camPos);

		updateCameraTransform();
	}
	else {
		//auto vrDevice = iris::VrManager::getDefaultDevice();

		float dirX = 0;
		float dirY = 0;
		if (KeyboardState::isKeyDown(Qt::Key_Left))
			dirX -= linearSpeed;

		// right
		if (KeyboardState::isKeyDown(Qt::Key_Right))
			dirX += linearSpeed;

		// up
		if (KeyboardState::isKeyDown(Qt::Key_Up))
			dirY -= linearSpeed;

		// down
		if (KeyboardState::isKeyDown(Qt::Key_Down))
			dirY += linearSpeed;


		// lock rot to yaw so user is always right side up
		auto yawRot = QQuaternion::fromEulerAngles(0, yaw, 0);
		viewer->setLocalRot(yawRot);

		// keyboard movement
		const QVector3D upVector(0, 1, 0);
		//not giving proper rotation when not in debug mode
		//apparently i need to normalize the head rotation quaternion
		auto rot = yawRot;
		rot.normalize();
		auto forwardVector = rot.rotatedVector(QVector3D(0, 0, -1));
		auto x = QVector3D::crossProduct(forwardVector, upVector).normalized();
		auto z = QVector3D::crossProduct(upVector, x).normalized();

		auto newDir = rot.rotatedVector(QVector3D(dirX, 0, dirY)) * 10;
		scene->getPhysicsEnvironment()->setDirection(QVector2D(newDir.x(), newDir.z()));
    }

    if (!!pickedNode && pickedNode->isPhysicsBody) {
        scene->getPhysicsEnvironment()->updatePickingConstraint(iris::PickingHandleType::MouseButton, iris::PhysicsHelper::btVector3FromQVector3D(calculateMouseRay(QPointF(mouseX, mouseY)) * 1024),
                                                                iris::PhysicsHelper::btVector3FromQVector3D(this->camera->getGlobalPosition()));
    }
}

void PlayerMouseController::doGodMode(float dt)
{
    auto linearSpeed = movementSpeed * dt;
    auto forwardVector = camera->getLocalRot().rotatedVector(QVector3D(0, 0, -1));
    auto sideVector = camera->getLocalRot().rotatedVector(QVector3D(1, 0, 0));
    //auto x = QVector3D::crossProduct(forwardVector,upVector).normalized();
    //auto z = QVector3D::crossProduct(upVector,x).normalized();

    auto x = sideVector;
    auto z = forwardVector;

    auto camPos = camera->getLocalPos();
    // left
    if(KeyboardState::isKeyDown(Qt::Key_Left))
        camPos -= x * linearSpeed;

    // right
    if(KeyboardState::isKeyDown(Qt::Key_Right))
        camPos += x * linearSpeed;

    // up
    if(KeyboardState::isKeyDown(Qt::Key_Up))
        camPos += z * linearSpeed;

    // down
    if(KeyboardState::isKeyDown(Qt::Key_Down))
        camPos -= z * linearSpeed;

    camera->setLocalPos(camPos);
    updateCameraTransform();
}

void PlayerMouseController::postUpdate(float dt)
{
	if (!!viewer) {
		updateCameraTransform();
		//camera->setLocalTransform(viewer->getGlobalTransform());
	}
}
