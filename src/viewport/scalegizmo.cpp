/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QQuaternion>
#include "viewport/scalegizmo.h"
#include <QApplication>

#include "irisgl/core/math/intersectionhelper.h"
#include "irisgl/core/math/mathhelper.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "services/services.h"
#include "services/sceneeditservice.h"
#include "viewport/gizmomeshes.h"
#include "commands/transformscenenodecommand.h"
#include "irisgl/core/math/mathhelper.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/panels/propertywidgets/transformpropertywidget.h"

#define DEFAULT_SNAP_LENGTH (1.0f)
#define CENTER_CIRCLE_RADIUS (0.015f)

ScaleHandle::ScaleHandle(Gizmo* gizmo, GizmoAxis axis)
{
	this->gizmo = gizmo;
	this->axis = axis;

	switch (axis) {
	case GizmoAxis::Center:
		handleExtent = QVector3D(0, 0, 0);
		//planes.append(QVector3D(0, 1, 0)); // this will change based on the view direction
		setHandleColor(QColor(255, 255, 255));
		break;
	case GizmoAxis::X:
		handleExtent = QVector3D(1, 0, 0);
		planes.append(QVector3D(0, 1, 0));
		planes.append(QVector3D(0, 0, 1));
		setHandleColor(QColor(237, 66, 66));
		break;
	case GizmoAxis::Y:
		handleExtent = QVector3D(0, 1, 0);
		planes.append(QVector3D(1, 0, 0));
		planes.append(QVector3D(0, 0, 1));
		setHandleColor(QColor(122, 204, 44));
		break;
	case GizmoAxis::Z:
		handleExtent = QVector3D(0, 0, 1);
		planes.append(QVector3D(1, 0, 0));
		planes.append(QVector3D(0, 1, 0));
		setHandleColor(QColor(58, 122, 240));
		break;
	}
}

bool ScaleHandle::isHit(QVector3D rayPos, QVector3D rayDir)
{
	auto gizmoTrans = gizmo->getTransform();

	if (this->axis == GizmoAxis::Center) {
		// sphere center intersection
		float t;
		QVector3D hitPoint;
		return iris::IntersectionHelper::raySphereIntersects(rayPos, rayDir, gizmoTrans * QVector3D(0, 0, 0), gizmo->getGizmoScale() * CENTER_CIRCLE_RADIUS, t, hitPoint);
	}
	else {

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
}

QVector3D ScaleHandle::getHitPos(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir)
{
	bool hit = false;
	QVector3D finalHitPos;
	float closestDist = 10000000;

	// transform rayPos and rayDir to gizmo space
	auto gizmoTransform = gizmo->getTransform();
	auto worldToGizmo = gizmoTransform.inverted();
	rayPos = worldToGizmo * rayPos;
	rayDir = QQuaternion::fromRotationMatrix(worldToGizmo.normalMatrix()).rotatedVector(rayDir);

	if (this->axis == GizmoAxis::Center) {
		// sphere center intersection
		float t;
		QVector3D hitPoint;
		//iris::IntersectionHelper::raySphereIntersects(rayPos, rayDir, QVector3D(0, 0, 0), 1, t, hitPoint);

		//return gizmoTransform * hitPoint;
		auto normal = QQuaternion::fromRotationMatrix(worldToGizmo.normalMatrix()).rotatedVector(-viewDir);
		iris::IntersectionHelper::intersectSegmentPlane(rayPos, rayPos + rayDir * 10000000, iris::Plane(normal, 0), t, hitPoint);

		return gizmoTransform * hitPoint;
	}
	else {
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
	handles.append(new ScaleHandle(this, GizmoAxis::Center));
	handles.append(new ScaleHandle(this, GizmoAxis::X));
	handles.append(new ScaleHandle(this, GizmoAxis::Y));
	handles.append(new ScaleHandle(this, GizmoAxis::Z));

	loadAssets();

	dragging = false;
	draggedHandle = nullptr;
	handleVisualScale = QVector3D(1, 1, 1);
}

void ScaleGizmo::loadAssets()
{
	// Procedural handles (gizmomeshes.cpp): thin axis lines ending in small
	// cubes, small cube at the core. Same local reach as the old OBJs, so the
	// analytic hit-testing is untouched.
	handleMeshes.append(GizmoMeshes::centerCube());
	handleMeshes.append(GizmoMeshes::scaleHandle(GizmoAxis::X));
	handleMeshes.append(GizmoMeshes::scaleHandle(GizmoAxis::Y));
	handleMeshes.append(GizmoMeshes::scaleHandle(GizmoAxis::Z));

	centerMesh = GizmoMeshes::centerCube();

}

bool ScaleGizmo::isDragging()
{
	return dragging;
}

void ScaleGizmo::startDragging(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir)
{
	draggedHandle = getHitHandle(rayPos, rayDir, viewDir, hitPos);
	if (draggedHandle == nullptr) {
		dragging = false; // end dragging if no handle was actually hit
		return;
	}

	handleVisualScale = QVector3D(1, 1, 1);
	nodeStartPos = selectedNode->getGlobalPosition();
	dragging = true;
	startScale = selectedNode->getLocalScale();
	hitDir = (hitPos - nodeStartPos).normalized();
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

void ScaleGizmo::drag(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir)
{
	if (draggedHandle == nullptr) {
		return;
	}

	auto slidingPos = draggedHandle->getHitPos(rayPos, rayDir, viewDir);

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
	case GizmoAxis::Center: {
		float length = diff.length();

		// determine whether or not to invert scale
		//QVector3D curDir = (slidingPos - nodeStartPos).normalized();
		//length = QVector3D::dotProduct(curDir, hitDir) > 0 ? length : -length;

		diff = QVector3D(length, length, length);
		handleVisualScale = QVector3D(
			qAbs(qBound(-2.0f, 1.0f + length *0.1f, 2.0f)),
			qAbs(qBound(-2.0f, 1.0f + length *0.1f, 2.0f)),
			qAbs(qBound(-2.0f, 1.0f + length *0.1f, 2.0f)));
		break;
	}
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
	if (services && services->sceneEdit) services->sceneEdit->notifyTransformChanged();
}

bool ScaleGizmo::isHit(QVector3D rayPos, QVector3D rayDir)
{
	for (auto i = 0; i< handles.size(); i++) {
		if (handles[i]->isHit(rayPos, rayDir))
		{
			return true;
		}
	}

	return false;
}

// returns hit position of the hit handle
ScaleHandle* ScaleGizmo::getHitHandle(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir, QVector3D& hitPos)
{
	ScaleHandle* closestHandle = nullptr;
	float closestDistance = 10000000;

	for (auto i = 0; i< handles.size(); i++)
	{
		if (handles[i]->isHit(rayPos, rayDir)) {
			auto hit = handles[i]->getHitPos(rayPos, rayDir, viewDir);// bad, move hitPos to ref variable
			auto dist = hitPos.distanceToPoint(rayPos);
			if (dist < closestDistance) {
				closestHandle = handles[i];
				closestDistance = dist;
				hitPos = hit;

				// center takes precedence over all
				if (closestHandle->axis == GizmoAxis::Center)
					break;
			}
		}
	}

	return closestHandle;
}

QVector<GizmoDrawItem> ScaleGizmo::drawItems(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir)
{
	QVector<GizmoDrawItem> items;
	if (!selectedNode) return items;
	const QColor highlight(255, 255, 0);
	if (dragging) {
		for (int i = 0; i < handles.size(); i++) {
			if (handles[i] != draggedHandle) continue;
			auto transform = this->getTransform();
			transform.scale(getGizmoScale() * handles[i]->handleScale * handleVisualScale);
			items.append({ handleMeshes[i], transform, highlight });
		}
		return items;
	}
	QVector3D hitPos;
	auto hitHandle = getHitHandle(rayPos, rayDir, viewDir, hitPos);
	for (int i = 0; i < handles.size(); i++) {
		auto transform = this->getTransform();
		transform.scale(getGizmoScale() * handles[i]->handleScale * handleVisualScale);
		items.append({ handleMeshes[i], transform, handles[i] == hitHandle ? highlight : handles[i]->getHandleColor() });
	}
	return items;
}
