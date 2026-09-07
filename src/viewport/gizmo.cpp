/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/vec.h"
#include "viewport/gizmo.h"

#include "irisgl/irisgl.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "commands/transformscenenodecommand.h"


Gizmo::Gizmo()
{
	transformSpace = GizmoTransformSpace::Local;
	gizmoScale = 1.0f;
}

// Screen-constant gizmo sizing.
//
// TWO BUGS IN ONE LINE, both live since 2016 and both fixed here (owner report
// 2026-09-07, root-caused on the rig):
//
//  1. DEGREES INTO qTan. `camera->angle` is the vertical angle of view in
//     DEGREES (cameranode.h: "Degrees are always used internally"), and qTan
//     takes RADIANS. At the default 45 the expression evaluated tan(22.5 rad)
//     = +0.557 — a positive number by pure luck, which is why the gizmo looked
//     roughly right forever. At fov 75 it evaluates tan(37.5 rad) = -0.199:
//     gizmoScale goes NEGATIVE, every handle transform is mirrored (the visual
//     flip pushTransform launders into a 180-degree rotation) and every hit
//     radius — `gizmo->getGizmoScale() * handleScale` in the three gizmos'
//     isHit — goes negative too, so no arrow can be picked at all.
//  2. IT DIVIDES. Screen-constant sizing means the gizmo's world size must
//     GROW with the angle of view: the same on-screen fraction of a wider
//     frustum is more world units. `d / tan(fov/2)` shrinks it instead.
//
// The corrected form is the textbook one — world size = distance * tan(half
// angle) * k — with k chosen so the default 45-degree camera keeps EXACTLY
// today's look: the old expression gave 1/tan(22.5 rad) = 1.7935 * d, and
// 4.33 * tan(22.5 deg) = 4.33 * 0.41421 = 1.7935. So nothing moves at 45 and
// every other fov becomes correct instead of arbitrary.
void Gizmo::updateSize(iris::CameraNodePtr camera)
{
	if (!!selectedNode) {
		if (camera->getProjection() == iris::CameraProjection::Perspective) {
			float distToCam = (selectedNode->getGlobalPosition() - camera->getGlobalPosition()).length();
			// Guard the ends of the range the way Ogre does (setFOVy is clamped
			// to (0, 180) upstream): a zero or 180-degree angle has no tangent.
			const float fovDeg = qBound(1.0f, camera->angle, 179.0f);
			gizmoScale = kGizmoScreenFraction * distToCam
			           * qTan(qDegreesToRadians(fovDeg * 0.5f));
		}
		else {
			//camera->orthoSize
			gizmoScale = camera->orthoSize * 5.0;
		}
	}
}

float Gizmo::getGizmoScale()
{
	return gizmoScale;
}

void Gizmo::setTransformSpace(GizmoTransformSpace transformSpace)
{
	this->transformSpace = transformSpace;
}

void Gizmo::setSelectedNode(iris::SceneNodePtr node)
{
	selectedNode = node;
}

void Gizmo::clearSelectedNode()
{
	selectedNode.clear();
}

void Gizmo::setInitialTransform()
{
	oldPos = selectedNode->getLocalPos();
	oldRot = selectedNode->getLocalRot();
	oldScale = selectedNode->getLocalScale();
}
void Gizmo::createUndoAction()
{
	auto newPos = selectedNode->getLocalPos();
	auto newRot = selectedNode->getLocalRot();
	auto newScale = selectedNode->getLocalScale();

	selectedNode->setLocalPos(oldPos);
	selectedNode->setLocalRot(oldRot);
	selectedNode->setLocalScale(oldScale);
	if (services && services->undo)
		services->undo->push(new TransformSceneNodeCommand(selectedNode, newPos, newRot, newScale));
}

iris::Vec3 Gizmo::snap(iris::Vec3 pos, float gridSize)
{
	return iris::Vec3(Gizmo::snap(pos.x(), gridSize),
					 Gizmo::snap(pos.y(), gridSize),
					 Gizmo::snap(pos.z(), gridSize));
}

float Gizmo::snap(float value, float gridSize)
{
	return qFloor(value / gridSize)*gridSize;
}

// returns transform of the gizmo, not the scene node
// the transform is calculated based on the transform's space (local or global)
iris::Mat4 Gizmo::getTransform()
{
	if (!selectedNode) {
		iris::Mat4 mat;
		mat.setToIdentity();
		return mat;
	}

	if (transformSpace == GizmoTransformSpace::Global) {
		iris::Mat4 trans;
		trans.setToIdentity();
		trans.translate(selectedNode->getGlobalPosition());
		return trans;
	}
	else {
		//todo: remove scale
		iris::Mat4 trans;
		trans.setToIdentity();
		trans.translate(selectedNode->getGlobalPosition());
		trans.rotate(selectedNode->getGlobalRotation().normalized());
		return trans;
	}
}

bool Gizmo::isHit(iris::Vec3 rayPos, iris::Vec3 rayDir)
{
	return false;
}