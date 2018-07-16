/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "scalegizmo.h"
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QApplication>

#include "irisgl/src/math/intersectionhelper.h"
#include "irisgl/src/math/mathhelper.h"
#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/graphics/vertexlayout.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/graphics/graphicsdevice.h"
#include "irisgl/src/graphics/graphicshelper.h"
#include "uimanager.h"
#include "../commands/transfrormscenenodecommand.h"
#include "irisgl/src/math/mathhelper.h"
#include "../widgets/scenenodepropertieswidget.h"
#include "../widgets/propertywidgets/transformpropertywidget.h"

#define DEFAULT_SNAP_LENGTH 1.0f

ScaleHandle::ScaleHandle(Gizmo* gizmo, GizmoAxis axis)
{
	this->gizmo = gizmo;
	this->axis = axis;

	switch (axis) {
	case GizmoAxis::X:
		handleExtent = QVector3D(1, 0, 0);
		planes.append(QVector3D(0, 1, 0));
		planes.append(QVector3D(0, 0, 1));
		setHandleColor(QColor(255, 0, 0));
		break;
	case GizmoAxis::Y:
		handleExtent = QVector3D(0, 1, 0);
		planes.append(QVector3D(1, 0, 0));
		planes.append(QVector3D(0, 0, 1));
		setHandleColor(QColor(0, 255, 0));
		break;
	case GizmoAxis::Z:
		handleExtent = QVector3D(0, 0, 1);
		planes.append(QVector3D(1, 0, 0));
		planes.append(QVector3D(0, 1, 0));
		setHandleColor(QColor(0, 0, 255));
		break;
	}
}

bool ScaleHandle::isHit(QVector3D rayPos, QVector3D rayDir)
{
	auto gizmoTrans = gizmo->getTransform();

	// calculate world space position of the segment representing the handle
	auto p1 = gizmoTrans * QVector3D(0, 0, 0);
	auto q1 = gizmoTrans * (handleExtent * handleLength * gizmo->getGizmoScale() * handleScale);

	auto p2 = rayPos;
	auto q2 = rayPos + rayDir * 100000;

	float s, t;
	QVector3D c1, c2;
	auto dist = iris::MathHelper::closestPointBetweenSegments(p1, q1, p2, q2, s, t, c1, c2);
	if (dist < handleScale * gizmo->getGizmoScale() * handleScale) {
		return true;
	}

	return false;
}

QVector3D ScaleHandle::getHitPos(QVector3D rayPos, QVector3D rayDir)
{
	bool hit = false;
	QVector3D finalHitPos;
	float closestDist = 10000000;

	// transform rayPos and rayDir to gizmo space
	auto gizmoTransform = gizmo->getTransform();
	auto worldToGizmo = gizmoTransform.inverted();
	rayPos = worldToGizmo * rayPos;
	rayDir = QQuaternion::fromRotationMatrix(worldToGizmo.normalMatrix()).rotatedVector(rayDir);

	// loop through planes
	for (auto normal : planes) {
		float t;
		QVector3D hitPos;

		// flip normal so its facing the ray source
		if (QVector3D::dotProduct(normal, rayPos) < 0)
			normal = -normal;

		if (iris::IntersectionHelper::intersectSegmentPlane(rayPos, rayPos + rayDir * 10000000, iris::Plane(normal, 0), t, hitPos)) {
			// ignore planes at grazing angles
			if (qAbs(QVector3D::dotProduct(rayDir, normal)) < 0.1f)
				continue;

			auto hitResult = handleExtent * hitPos;

			// this isnt the first hit, but if it's the closest one then use it
			if (hit) {
				if (rayPos.distanceToPoint(hitResult) < closestDist) {
					finalHitPos = hitResult;
					closestDist = rayPos.distanceToPoint(hitResult);
				}
			}
			else {
				// first hit, just assign it
				finalHitPos = hitResult;
				closestDist = rayPos.distanceToPoint(hitResult);
			}

			hit = true;
		}
	}

	if (!hit) {
		// no hit so move to max distance in view direction
		float dominantExtent = iris::MathHelper::sign(QVector3D::dotProduct(rayDir.normalized(), handleExtent));// results in -1 or 1
		finalHitPos = dominantExtent * handleExtent * 10000;
	}

	// now convert it back to world space
	finalHitPos = gizmoTransform * finalHitPos;

	return finalHitPos;
}

ScaleGizmo::ScaleGizmo() :
	Gizmo()
{
	handles[0] = new ScaleHandle(this, GizmoAxis::X);
	handles[1] = new ScaleHandle(this, GizmoAxis::Y);
	handles[2] = new ScaleHandle(this, GizmoAxis::Z);

	loadAssets();

	dragging = false;
	draggedHandle = nullptr;
	handleVisualScale = QVector3D(1, 1, 1);
}

void ScaleGizmo::loadAssets()
{
	handleMeshes.append(iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/models/scale_x.obj")));
	handleMeshes.append(iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/models/scale_y.obj")));
	handleMeshes.append(iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/models/scale_z.obj")));

	centerMesh = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/models/axis_cube.obj"));

	shader = iris::GraphicsHelper::loadShader(
		IrisUtils::getAbsoluteAssetPath("app/shaders/gizmo.vert"),
		IrisUtils::getAbsoluteAssetPath("app/shaders/gizmo.frag"));
}

bool ScaleGizmo::isDragging()
{
	return dragging;
}

void ScaleGizmo::startDragging(QVector3D rayPos, QVector3D rayDir)
{
	draggedHandle = getHitHandle(rayPos, rayDir, hitPos);
	if (draggedHandle == nullptr) {
		dragging = false; // end dragging if no handle was actually hit
		return;
	}

	handleVisualScale = QVector3D(1, 1, 1);
	nodeStartPos = selectedNode->getGlobalPosition();
	dragging = true;
	startScale = selectedNode->getLocalScale();
	setInitialTransform();
}

void ScaleGizmo::endDragging()
{
	dragging = false;
	draggedHandle = nullptr;
	handleVisualScale = QVector3D(1, 1, 1);

	// undo-redo
	createUndoAction();
}

void ScaleGizmo::drag(QVector3D rayPos, QVector3D rayDir)
{
	if (draggedHandle == nullptr) {
		return;
	}

	auto slidingPos = draggedHandle->getHitPos(rayPos, rayDir);

	// move node along line
	// do snapping here as well
	QVector3D diff = slidingPos - hitPos;
	auto mods = QApplication::keyboardModifiers();
	if (mods.testFlag(Qt::ControlModifier)) {
		float length = diff.length();
		float snapLength = Gizmo::snap(length, DEFAULT_SNAP_LENGTH);
		diff = diff.normalized() * snapLength;
	}

	switch (draggedHandle->axis)
	{
	case GizmoAxis::X:
		diff = QVector3D(diff.x(), 0, 0);
		handleVisualScale = QVector3D(qAbs(qBound(-2.0f, 1.0f + diff.x()*0.1f, 2.0f)), 1, 1);
		break;
	case GizmoAxis::Y:
		diff = QVector3D(0, diff.y(), 0);
		handleVisualScale = QVector3D(1, qAbs(qBound(-2.0f, 1.0f + diff.y()*0.1f, 2.0f)), 1);
		break;
	case GizmoAxis::Z:
		diff = QVector3D(0, 0, diff.z());
		handleVisualScale = QVector3D(1, 1, qAbs(qBound(-2.0f, 1.0f + diff.z()*0.1f, 2.0f)));
		break;
	}

	selectedNode->setLocalScale(startScale + diff);
	UiManager::propertyWidget->refreshTransform();
}

bool ScaleGizmo::isHit(QVector3D rayPos, QVector3D rayDir)
{
	for (auto i = 0; i< 3; i++) {
		if (handles[i]->isHit(rayPos, rayDir))
		{
			return true;
		}
	}

	return false;
}

// returns hit position of the hit handle
ScaleHandle* ScaleGizmo::getHitHandle(QVector3D rayPos, QVector3D rayDir, QVector3D& hitPos)
{
	ScaleHandle* closestHandle = nullptr;
	float closestDistance = 10000000;

	for (auto i = 0; i<3; i++)
	{
		if (handles[i]->isHit(rayPos, rayDir)) {
			auto hit = handles[i]->getHitPos(rayPos, rayDir);// bad, move hitPos to ref variable
			auto dist = hitPos.distanceToPoint(rayPos);
			if (dist < closestDistance) {
				closestHandle = handles[i];
				closestDistance = dist;
				hitPos = hit;
			}
		}
	}

	return closestHandle;
}

void ScaleGizmo::render(iris::GraphicsDevicePtr device, QVector3D rayPos, QVector3D rayDir, QMatrix4x4& viewMatrix, QMatrix4x4& projMatrix)
{
	auto gl = device->getGL();
	gl->glClear(GL_DEPTH_BUFFER_BIT);
	shader->bind();

	shader->setUniformValue("u_viewMatrix", viewMatrix);
	shader->setUniformValue("u_projMatrix", projMatrix);
	shader->setUniformValue("showHalf", false);

	if (dragging) {
		for (int i = 0; i < 3; i++) {
			if (handles[i] == draggedHandle) {
				auto transform = this->getTransform();
				transform.scale(getGizmoScale() * handles[i]->handleScale * handleVisualScale);
				shader->setUniformValue("color", QColor(255, 255, 0));
				shader->setUniformValue("u_worldMatrix", transform);

				handleMeshes[i]->draw(device);
			}
		}
	}
	else {
		QVector3D hitPos;
		auto hitHandle = getHitHandle(rayPos, rayDir, hitPos);

		for (int i = 0; i < 3; i++) {
			auto transform = this->getTransform();
			transform.scale(getGizmoScale() * handles[i]->handleScale * handleVisualScale);
			shader->setUniformValue("u_worldMatrix", transform);

			if (handles[i] == hitHandle)
				shader->setUniformValue("color", QColor(255, 255, 0));
			else
				shader->setUniformValue("color", handles[i]->getHandleColor());
			handleMeshes[i]->draw(device);
		}

		shader->setUniformValue("color", QColor(255, 255, 255));
		centerMesh->draw(device);
	}

	shader->release();
}