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

// generic update function
void Gizmo::updateSize(iris::CameraNodePtr camera)
{ 
	if (!!selectedNode) {
		if (camera->getProjection() == iris::CameraProjection::Perspective) {
			float distToCam = (selectedNode->getGlobalPosition() - camera->getGlobalPosition()).length();
			gizmoScale = distToCam / (qTan(camera->angle / 2.0f));
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