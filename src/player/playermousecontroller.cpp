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
#include "viewport/cameraspeed.h"
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


// TAKING A CAMERA OVER: read its heading, write nothing.
//
// PlayBack calls this when it binds a controller and, since PLAYER-SPAWN-1,
// whenever the camera the run flies CHANGES under it (the play edge that
// switches to the scene's armed camera, and the stop that hands the free
// viewer back).
//
// THE SECOND HALF OF THIS PAIR IS GONE (CRUD, 2026-09-17): `end()` used to put
// the camera back where start() found it, behind a `shouldRestoreCameraTransform`
// flag that nothing ever cleared and a call PlayBack could not make — the one
// `camController->end()` in setController only fires when the controller
// POINTER changes, and PlayBack has exactly one controller for its whole life.
// It was a second, unreachable copy of a restore that two live mechanisms
// already do: PlayBack::restoreNodeTransforms puts every node back at stop,
// and EnginePlayerScene::end puts the play camera's pose and lens back when
// the page goes.
void PlayerMouseController::start()
{
	captureYawPitchRollFromCamera();
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
            // THE POLE IS OUT OF REACH (the lead's fix at PLAYER-SPAWN-1's merge):
            // the controller re-reads the camera's heading on every idle frame
            // now, and a pitch dragged past 90 decomposes back as (180 - pitch,
            // yaw + 180, roll 180) — the same rotation, the other decomposition —
            // which inverts the next drag's pitch sense and rolls a free viewer
            // by 180. The editor's controller clamps for the same reason
            // (editorcameracontroller.cpp); a hair inside the pole keeps the
            // decomposition unambiguous.
            this->pitch = this->pitch < -89.5f ? -89.5f : (this->pitch > 89.5f ? 89.5f : this->pitch);
            // Written only while the right button drives the look: a hover used
            // to stamp the last-captured heading onto the camera on every move,
            // a Transform mark per hover on an armed, authored camera.
            updateCameraTransform();
        }
    }
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

// The wheel steps the camera speed while the RIGHT BUTTON is held — the
// editor's gesture (EditorCameraController::onMouseWheel) on THE one dial
// (CameraSpeed, owner R15: the Player had a second multiplier of its own until
// this lane), with the same two strides: one notch is one, and five with SHIFT
// held, which is what the toolbar button's tooltip promises on both surfaces.
// It did nothing at all before; the player has no dolly to conflict with.
//
// A NOTCH, not an event (WheelNotches), for the reason the editor's does it:
// a trackpad delivers fractions of a notch per event.
//
// NOTHING IS TOLD FROM HERE: the dial announces itself (CameraSpeed::
// setOnChanged) and the shell's one handler re-syncs the toolbar. This used to
// call a per-controller `onSpeedChanged` hook that was assigned NOWHERE, so
// stepping the speed in the Player left the editor's button showing the old
// number until something else happened to move it.
void PlayerMouseController::onMouseWheel(int delta)
{
    if (!rightMouseDown) return;
    const int notches = speedWheel.accumulate(delta);
    if (notches == 0) return;
    const int stride = KeyboardState::isKeyDown(Qt::Key_Shift) ? 5 : 1;
    CameraSpeed::step(notches * stride);
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
	// THE ROLL IS THE CAMERA'S OWN (PLAYER-SPAWN-1 rule 3). This wrote a hard
	// zero, which is right for a free viewer — mouse-look never rolls one, and
	// the value captured below is therefore always 0 for one — and wrong for an
	// AUTHORED camera, which the play controller now flies when the scene has
	// an active one: a dutch angle somebody keyed would be levelled by the
	// first frame of flight. Captured, kept, written back.
	camera->setLocalRot(iris::Quat::fromEulerAngles(pitch, yaw, roll));
    camera->update(0);
}

void PlayerMouseController::captureYawPitchRollFromCamera()
{
	// capture yaw, pitch and roll from whatever the camera is doing NOW
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
    const float linearSpeed = CameraSpeed::applyTo(15.0f) * dt;
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
	if (!flyThisFrame(heldFlyKeys(), linearSpeed) && camera) {
		// Nothing held: the run's own camera is left exactly as the document
		// put it (see flyThisFrame).
		camera->update(0);
	}

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
//   * The speed is this surface's own base (25 u/s, `movementSpeed`) times the
//     persisted CameraSpeed factor, stepped by the toolbar's speed button and
//     by the wheel while the right button is held (onMouseWheel).
void PlayerMouseController::doGodMode(float dt)
{
    const float linearSpeed = CameraSpeed::applyTo(movementSpeed) * dt;
    // SAME STEP AS PLAY MODE, and as the editor's (S13): viewport/flystep.h.
    // This function's own version differed in one detail nobody wanted — it
    // strafed along the camera's ROLLED right rather than a horizontal one.
    if (!flyThisFrame(heldFlyKeys(), linearSpeed) && camera) camera->update(0);
}

// ONE FRAME OF FREE FLIGHT, AND NOTHING AT ALL WHEN NOTHING IS HELD
// (PLAYER-SPAWN-1 rule 3, 2026-09-17).
//
// Both fly paths used to write the camera's POSITION and its ROTATION on every
// frame, held keys or not — a zero-length step, then a rotation rebuilt from
// this controller's own yaw and pitch. That is invisible while the only camera
// a player ever flies is the free viewer nothing else writes to, and it is a
// silent overwrite the moment the run flies the ACTIVE camera: a keyframed
// pan, a camera riding a socket or an avatar's head would be stamped flat with
// the heading the mouse-look happened to hold, sixty times a second, with the
// fly keys untouched.
//
// So an idle frame writes NOTHING and READS instead: the controller tracks
// whatever moved the camera, and the first flown frame therefore continues
// from where the camera actually is rather than snapping back to a heading
// from before the cut.
///
/// Returns true when this frame actually flew the camera.
bool PlayerMouseController::flyThisFrame(const flystep::Keys &keys, float linearSpeed)
{
    if (!camera) return false;
    if (!keys.any()) {
        captureYawPitchRollFromCamera();
        return false;
    }
    camera->setLocalPos(camera->getLocalPos()
                        + flystep::direction(camera->getLocalRot(), keys) * linearSpeed);
    updateCameraTransform();
    return true;
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
