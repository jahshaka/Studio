/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QQuaternion>
#include "translationgizmo.h"
#include <QApplication>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/core/math/intersectionhelper.h"
#include "irisgl/core/math/mathhelper.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/core/math/mathhelper.h"
#include "../uimanager.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "gizmomeshes.h"

#define DEFAULT_SNAP_LENGTH (1.0f)
#define CENTER_CIRCLE_RADIUS (0.015f)

TranslationHandle::TranslationHandle(Gizmo* gizmo, GizmoAxis axis)
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

bool TranslationHandle::isHit(QVector3D rayPos, QVector3D rayDir)
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
		auto q2 = rayPos + rayDir * 100000;// (nick) use segment instead of ray pos and ray dir

		float s, t;
		QVector3D c1, c2;
		auto dist = iris::MathHelper::closestPointBetweenSegments(p1, q1, p2, q2, s, t, c1, c2);
		if (dist < handleScale * gizmo->getGizmoScale() * handleScale) {
			return true;
		}

		return false;
	}
}

QVector3D TranslationHandle::getHitPos(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir)
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
		float dominantExtent = iris::MathHelper::sign(QVector3D::dotProduct(rayDir.normalized(), handleExtent));// results in -1 or 1
		finalHitPos = dominantExtent * handleExtent * 10000;
	}

	// now convert it back to world space
	finalHitPos = gizmoTransform * finalHitPos;

	return finalHitPos;
}

TranslationGizmo::TranslationGizmo() :
	Gizmo()
{
	handles.append(new TranslationHandle(this, GizmoAxis::Center));
	handles.append(new TranslationHandle(this, GizmoAxis::X));
	handles.append(new TranslationHandle(this, GizmoAxis::Y));
	handles.append(new TranslationHandle(this, GizmoAxis::Z));

	loadAssets();

	dragging = false;
	draggedHandle = nullptr;
}

void TranslationGizmo::loadAssets()
{
	// Procedural handles (gizmomeshes.cpp): thin axis lines ending in small
	// cones, small ball at the core. Same local reach as the old OBJs, so the
	// analytic hit-testing below is untouched.
	handleMeshes.append(GizmoMeshes::centerSphere());
	handleMeshes.append(GizmoMeshes::translateHandle(GizmoAxis::X));
	handleMeshes.append(GizmoMeshes::translateHandle(GizmoAxis::Y));
	handleMeshes.append(GizmoMeshes::translateHandle(GizmoAxis::Z));

	centerMesh = GizmoMeshes::centerSphere();

	// create circle (kept CPU-side; unused by the engine path today)
	QVector<float> points;
	for (float i = 0; i < 360; i += 1) {
		auto x = qCos(qDegreesToRadians(i));
		auto y = qSin(qDegreesToRadians(i));
		points.append(x); points.append(y); points.append(0);

		x = qCos(qDegreesToRadians(i + 1));
		y = qSin(qDegreesToRadians(i + 1));
		points.append(x); points.append(y); points.append(0);
	}

	iris::VertexLayout layout;
	layout.addAttrib(iris::VertexAttribUsage::Position, iris::AttribTypeFloat, 3, sizeof(float) * 3);

	auto vb = iris::VertexBuffer::create(layout);
	vb->setData((void*)points.constData(), points.size() * sizeof(float));

	circleMesh = iris::Mesh::create();
	circleMesh->addVertexBuffer(vb);
	circleMesh->setPrimitiveMode(iris::PrimitiveMode::Lines);
	circleMesh->setVertexCount(360 * 2);
}

bool TranslationGizmo::isDragging()
{
	return dragging;
}

void TranslationGizmo::startDragging(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir)
{
	draggedHandle = getHitHandle(rayPos, rayDir, viewDir, hitPos);
	if (draggedHandle == nullptr) {
		dragging = false; // end dragging if no handle was actually hit
		return;
	}

	nodeStartPos = selectedNode->getGlobalPosition();
	dragging = true;
	setInitialTransform();
}

void TranslationGizmo::endDragging()
{
	dragging = false;
	draggedHandle = nullptr;

	// undo-redo
	createUndoAction();
}

void TranslationGizmo::drag(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir)
{
	if (draggedHandle == nullptr) {
		return;
	}

	auto slidingPos = draggedHandle->getHitPos(rayPos, rayDir, viewDir);

	// move node along line
	// do snapping here as well
	auto diff = slidingPos - hitPos;

	// apply snapping (relative snapping)
	auto mods = QApplication::keyboardModifiers();
	if (mods.testFlag(Qt::ControlModifier)) {
		float length = diff.length();
		float snapLength = Gizmo::snap(length, DEFAULT_SNAP_LENGTH);
		diff = diff.normalized() * snapLength;
	}

	// apply diff in global space
	auto targetPos = nodeStartPos + diff;

	// bring to local space
	auto localTarget = selectedNode->parent->getGlobalTransform().inverted() * targetPos;
	
	//selectedNode->setLocalPos(localTarget);
	selectedNode->setGlobalPos(targetPos);
	UiManager::propertyWidget->refreshTransform();
}

bool TranslationGizmo::isHit(QVector3D rayPos, QVector3D rayDir)
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
TranslationHandle* TranslationGizmo::getHitHandle(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir, QVector3D& hitPos)
{
	TranslationHandle* closestHandle = nullptr;
	float closestDistance = 10000000;

	for (auto i = 0; i< handles.size(); i++)
	{
		if (handles[i]->isHit(rayPos, rayDir)) {
			auto hit = handles[i]->getHitPos(rayPos, rayDir, viewDir);// bad, move hitPos to ref variable
			auto dist = hitPos.distanceToPoint(rayPos);
			//irisDebug() << "hit handle " << i;
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

QVector<GizmoDrawItem> TranslationGizmo::drawItems(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir)
{
	QVector<GizmoDrawItem> items;
	if (!selectedNode) return items;
	const QColor highlight(255, 255, 0);
	if (dragging) {
		for (int i = 0; i < handles.size(); i++) {
			if (handles[i] != draggedHandle) continue;
			auto transform = this->getTransform();
			transform.scale(getGizmoScale() * handles[i]->handleScale);
			items.append({ handleMeshes[i], transform, highlight });
		}
		return items;
	}
	QVector3D hitPos;
	auto hitHandle = getHitHandle(rayPos, rayDir, viewDir, hitPos);
	for (int i = 0; i < handles.size(); i++) {
		auto transform = this->getTransform();
		transform.scale(getGizmoScale() * handles[i]->handleScale);
		items.append({ handleMeshes[i], transform, handles[i] == hitHandle ? highlight : handles[i]->getHandleColor() });
	}
	return items;
}
