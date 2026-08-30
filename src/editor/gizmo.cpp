/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "gizmo.h"

#include "irisgl/irisgl.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "uimanager.h"
#include "../commands/transfrormscenenodecommand.h"


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
	UiManager::pushUndoStack(new TransformSceneNodeCommand(selectedNode, newPos, newRot, newScale));
}

QVector3D Gizmo::snap(QVector3D pos, float gridSize)
{
	return QVector3D(Gizmo::snap(pos.x(), gridSize),
					 Gizmo::snap(pos.y(), gridSize),
					 Gizmo::snap(pos.z(), gridSize));
}

float Gizmo::snap(float value, float gridSize)
{
	return qFloor(value / gridSize)*gridSize;
}

// returns transform of the gizmo, not the scene node
// the transform is calculated based on the transform's space (local or global)
QMatrix4x4 Gizmo::getTransform()
{
	if (!selectedNode) {
		QMatrix4x4 mat;
		mat.setToIdentity();
		return mat;
	}

	if (transformSpace == GizmoTransformSpace::Global) {
		QMatrix4x4 trans;
		trans.setToIdentity();
		trans.translate(selectedNode->getGlobalPosition());
		return trans;
	}
	else {
		//todo: remove scale
		QMatrix4x4 trans;
		trans.setToIdentity();
		trans.translate(selectedNode->getGlobalPosition());
		trans.rotate(selectedNode->getGlobalRotation().normalized());
		return trans;
	}
}

bool Gizmo::isHit(QVector3D rayPos, QVector3D rayDir)
{
	return false;
}