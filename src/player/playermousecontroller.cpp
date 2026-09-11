/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "player/playermousecontroller.h"
#include "viewport/keyboardstate.h"
#include "viewport/flyspeedsettings.h"
#include "viewport/flystep.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/physics/physicshelper.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/core/geometry/trimesh.h"
#include "irisgl/core/viewport.h" 
#include "irisgl/core/geometry/trimesh.h"


void PlayerMouseController::start()
{
    // capture cam transform
    camPos = camera->getLocalPos();
    camRot = camera->getLocalRot();

	// capture yaw and pitch
	float roll;
	camera->getLocalRot().getEulerAngles(&pitch, &yaw, &roll);
}

void PlayerMouseController::end()
{
	if (shouldRestoreCameraTransform) {
		// restore cam transform
		camera->setLocalPos(camPos);
		camera->setLocalRot(camRot);
	}
	camera->update(0);
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
                scene->getPhysicsEnvironment()->updatePickingConstraint(iris::PickingHandleType::MouseButton, iris::PhysicsHelper::btVector3FromVec3(calculateMouseRay(QPointF(mouseX, mouseY)) * 1024),
                                                                        iris::PhysicsHelper::btVector3FromVec3(this->camera->getGlobalPosition()));
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

iris::Vec3 PlayerMouseController::calculateMouseRay(const QPointF& pos)
{
    float x = pos.x();
    float y = pos.y();

    // viewport -> NDC
    float mousex = (2.0f * x) / this->viewport.width - 1.0f;
    float mousey = (2.0f * y) / this->viewport.height - 1.0f;
    iris::Vec2 NDC = iris::Vec2(mousex, -mousey);

    // NDC -> HCC
    iris::Vec4 HCC = iris::Vec4(NDC, -1.0f, 1.0f);

    // HCC -> View Space
    iris::Mat4 projection_matrix_inverse = this->camera->projMatrix.inverted();
    iris::Vec4 eye_coords = projection_matrix_inverse * HCC;
    iris::Vec4 ray_eye = iris::Vec4(eye_coords.x(), eye_coords.y(), -1.0f, 0.0f);

    // View Space -> World Space
    iris::Mat4 view_matrix_inverse = this->camera->viewMatrix.inverted();
    iris::Vec4 world_coords = view_matrix_inverse * ray_eye;
    iris::Vec3 final_ray_coords = iris::Vec3(world_coords);

    return final_ray_coords.normalized();
}

// The wheel steps the free camera's speed while the RIGHT BUTTON is held — the
// editor's gesture (EditorCameraController::onMouseWheel), on the player's own
// FlySpeedSettings surface. It did nothing at all before; the player has no
// dolly to conflict with.
void PlayerMouseController::onMouseWheel(int delta)
{
    if (!rightMouseDown || delta == 0) return;
    FlySpeedSettings::step(FlySpeedSettings::Player, delta > 0 ? 1 : -1);
    if (onSpeedChanged) onSpeedChanged();
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
    while (pickedRoot->isAttached() && pickedRoot->hasParent())
        pickedRoot = pickedRoot->getParent();

    if (pickedNode->isPhysicsBody) {
        scene->getPhysicsEnvironment()->createPickingConstraint(iris::PickingHandleType::MouseButton,
                                                                pickedNode->getGUID(),
                                                                iris::PhysicsHelper::btVector3FromVec3(hitList.last().hitPoint),
                                                                segStart,
                                                                segEnd);
        this->pickedNode = pickedNode;
    }
}

void PlayerMouseController::doScenePicking(const QSharedPointer<iris::SceneNode>& sceneNode,
                                     const iris::Vec3& segStart,
                                     const iris::Vec3& segEnd,
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
            auto invTransform = meshNode->getGlobalTransform().inverted();
            auto a = invTransform * segStart;
            auto b = invTransform * segEnd;

            QList<iris::TriangleIntersectionResult> results;
            if (int resultCount = triMesh->getSegmentIntersections(a, b, results)) {
                for (auto triResult : results) {
                    // convert hit to world space
                    auto hitPoint = meshNode->getGlobalTransform() * triResult.hitPoint;

                    PickingResult pick;
                    pick.hitNode = sceneNode;
                    pick.hitPoint = hitPoint;
                    pick.distanceFromCameraSqrd = (hitPoint - camera->getGlobalPosition()).lengthSquared();

                    hitList.append(pick);
                }
            }
        }
    }

    const int kids = sceneNode->childCount();
    for (int i = 0; i < kids; ++i)
        if (iris::SceneNode *c = sceneNode->childAt(i))
            doScenePicking(c->sharedFromThis(), segStart, segEnd, hitList);
}

iris::Vec3 PlayerMouseController::screenSpaceToWoldSpace(const QPointF& pos, float depth)
{
    float x = pos.x();
    float y = pos.y();
    // viewport -> NDC
    float mousex = (2.0f * x) / this->viewport.width - 1.0f;
    float mousey = (2.0f * y) / this->viewport.height - 1.0f;
    iris::Vec2 NDC = iris::Vec2(mousex, -mousey);

    // NDC -> HCC
    iris::Vec4 HCC = iris::Vec4(NDC, depth, 1.0f);

    // HCC -> View Space
    iris::Mat4 projection_matrix_inverse = this->camera->projMatrix.inverted();
    iris::Vec4 eye_coords = projection_matrix_inverse * HCC;
    //iris::Vec4 ray_eye = iris::Vec4(eye_coords.x(), eye_coords.y(), eye_coords.z(), 0.0f);

    // View Space -> World Space
    iris::Mat4 view_matrix_inverse = this->camera->viewMatrix.inverted();
    iris::Vec4 world_coords = view_matrix_inverse * eye_coords;


    return world_coords.toVector3D() / world_coords.w();
}

void PlayerMouseController::setViewport(const iris::Viewport &viewport)
{
    this->viewport = viewport;
}

void PlayerMouseController::updateCameraTransform()
{
	camera->setLocalRot(iris::Quat::fromEulerAngles(pitch, yaw, 0));
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
}

void PlayerMouseController::update(float dt)
{
    const float linearSpeed =
        15.0f * FlySpeedSettings::multiplier(FlySpeedSettings::Player) * dt;
    if (!_isPlaying) {
        this->doGodMode(dt);
		return;
    }
	// PLAY-MODE FLY, along the camera's TRUE forward (owner smoke S13,
	// 2026-09-11: "WASD/arrows in the player are locked to XY and ignore the
	// camera's facing"). It used to fly along the forward PROJECTED onto the
	// ground plane — cross(up, cross(forward, up)) — so looking down and
	// pressing W walked over the floor instead of descending, while the free
	// camera two functions down already flew properly. One definition now:
	// viewport/flystep.h, the editor's own step, which also keeps the strafe
	// horizontal and defines it at the poles.
	//
	// The other half of this branch drove the removed viewer node's character
	// controller (AVATAR_LOCOMOTION_SPEC Stage 0); piloted movement comes back
	// as its own component in Stage 2.
	camera->setLocalPos(camera->getLocalPos()
	                    + flystep::direction(camera->getLocalRot(), heldFlyKeys())
	                          * linearSpeed);

	updateCameraTransform();

    if (!!pickedNode && pickedNode->isPhysicsBody) {
        scene->getPhysicsEnvironment()->updatePickingConstraint(iris::PickingHandleType::MouseButton, iris::PhysicsHelper::btVector3FromVec3(calculateMouseRay(QPointF(mouseX, mouseY)) * 1024),
                                                                iris::PhysicsHelper::btVector3FromVec3(this->camera->getGlobalPosition()));
    }
}

// The player's FREE CAMERA (the scene is loaded but not playing).
//
// TWO CHANGES, 2026-09-07 (owner request):
//   * W/A/S/D are ALIASES of the arrow keys here, exactly as the arrows became
//     aliases of W/A/S/D in the editor fly. The player shipped with arrows
//     only, the editor with WASD only, and moving between the two spaces meant
//     changing hands — one lookup table, both spellings, in both places.
//   * The speed is the persisted FlySpeedSettings multiplier on this surface's
//     own base (25 u/s, `movementSpeed`), stepped by the toolbar dropdown and
//     by the wheel while the right button is held (onMouseWheel).
void PlayerMouseController::doGodMode(float dt)
{
    const float linearSpeed =
        movementSpeed * FlySpeedSettings::multiplier(FlySpeedSettings::Player) * dt;
    // SAME STEP AS PLAY MODE, and as the editor's (S13): viewport/flystep.h.
    // This function's own version differed in one detail nobody wanted — it
    // strafed along the camera's ROLLED right rather than a horizontal one.
    camera->setLocalPos(camera->getLocalPos()
                        + flystep::direction(camera->getLocalRot(), heldFlyKeys())
                              * linearSpeed);
    updateCameraTransform();
}

// WHAT IS HELD, as flight intentions — both spellings, one table (the arrow
// keys and W/A/S/D are aliases on this surface, as they are in the Assets
// preview; the editor answers to the arrows alone). Q/E are the vertical pair,
// exactly as flystep::HeldKeys spells them for the preview.
flystep::Keys PlayerMouseController::heldFlyKeys()
{
    const auto held = [](int a, int b) {
        return KeyboardState::isKeyDown(a) || KeyboardState::isKeyDown(b);
    };
    flystep::Keys keys;
    keys.forward = held(Qt::Key_Up,    Qt::Key_W);
    keys.back    = held(Qt::Key_Down,  Qt::Key_S);
    keys.left    = held(Qt::Key_Left,  Qt::Key_A);
    keys.right   = held(Qt::Key_Right, Qt::Key_D);
    keys.up      = held(Qt::Key_E,     Qt::Key_PageUp);
    keys.down    = held(Qt::Key_Q,     Qt::Key_PageDown);
    return keys;
}

void PlayerMouseController::postUpdate(float dt)
{
	// Nothing to do: this only ever re-pushed the removed viewer node's
	// transform onto the camera (AVATAR_LOCOMOTION_SPEC Stage 0). update()
	// already calls updateCameraTransform for the free-fly path.
	Q_UNUSED(dt);
}
