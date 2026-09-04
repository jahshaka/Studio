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
#include "viewport/rotationgizmo.h"
#include <QApplication>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/core/math/intersectionhelper.h"
#include "irisgl/core/math/mathhelper.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "commands/transformscenenodecommand.h"
#include "irisgl/core/math/mathhelper.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "services/services.h"
#include "services/sceneeditservice.h"
#include "viewport/gizmomeshes.h"

#include "viewport/snapsettings.h"

RotationHandle::RotationHandle(Gizmo* gizmo, GizmoAxis axis)
{
	this->gizmo = gizmo;
	this->axis = axis;

	switch (axis) {
	case GizmoAxis::X:
		handleExtent = iris::Vec3(1, 0, 0);
		plane = iris::Vec3(1, 0, 0);
		setHandleColor(QColor(237, 66, 66));
		break;
	case GizmoAxis::Y:
		handleExtent = iris::Vec3(0, 1, 0);
		plane = iris::Vec3(0, 1, 0);
		setHandleColor(QColor(122, 204, 44));
		break;
	case GizmoAxis::Z:
		handleExtent = iris::Vec3(0, 0, 1);
		plane = iris::Vec3(0, 0, 1);
		setHandleColor(QColor(58, 122, 240));
		break;
	}
}

bool RotationHandle::isHit(iris::Vec3 rayPos, iris::Vec3 rayDir)
{
	auto gizmoTransform = gizmo->getTransform();
	gizmoTransform.scale(handleScale * gizmo->getGizmoScale());
	auto worldToGizmo = gizmoTransform.inverted();

	rayPos = worldToGizmo * rayPos;
	//rayDir = iris::Quat::fromRotationMatrix(worldToGizmo.normalMatrix()).normalized() * rayDir;
	rayDir = (worldToGizmo * iris::Vec4(rayDir, 0)).toVector3D();

	iris::Vec3 hitPoint;
	float hitDist;
	iris::Vec3 hitPlane;
	if (iris::Vec3::dotProduct(plane, rayDir) < 0)
		hitPlane = -plane;
	else
		hitPlane = plane;

	if (iris::IntersectionHelper::intersectSegmentPlane(rayPos, rayPos + rayDir * 1000000, iris::Plane(hitPlane, 0), hitDist, hitPoint)) {
		//float innerRadius = (handleRadius - (handleRadiusSize / 2.0f)) *handleScale * gizmo->getGizmoScale();
		//float outerRadius = (handleRadius + (handleRadiusSize / 2.0f)) *handleScale * gizmo->getGizmoScale();
		float innerRadius = (handleRadius - (handleRadiusSize / 2.0f));
		float outerRadius = (handleRadius + (handleRadiusSize / 2.0f));
		float distToCenter = hitPoint.length();
		//qDebug() << distToCenter;

		// hit should be from the part facing the point, not the back side
		if (iris::Vec3::dotProduct(hitPoint.normalized(), rayPos.normalized()) < 0)
			return false;

		if (distToCenter > innerRadius && distToCenter < outerRadius) {
			//qDebug() << "hit: " << distToCenter;
			//qDebug() << "hit: " << hitPoint;
			//qDebug() << "hit!";
			return true;
		}

		return false;
	}

	return false;
}

bool RotationHandle::getHitAngle(iris::Vec3 rayPos, iris::Vec3 rayDir, float& angle)
{
	auto gizmoTransform = gizmo->getTransform();
	auto worldToGizmo = gizmoTransform.inverted();

	rayPos = worldToGizmo * rayPos;
	rayDir = iris::Quat::fromRotationMatrix(worldToGizmo.normalMatrix()).normalized() * rayDir;

	iris::Vec3 hitPoint;
	float hitDist;	

	// first test of the handle's facing plane
	if (iris::IntersectionHelper::intersectSegmentPlane(rayPos, rayPos + rayDir * 1000000, iris::Plane(plane, 0), hitDist, hitPoint)) {
		hitPoint.normalize();
	}
	else {
		// if the handle's plane miss, then test against plane that always faces the user
		auto facingPlane = -(rayPos.normalized());
		// this is a guaranteed hit since the plane is facing the ray source
		iris::IntersectionHelper::intersectSegmentPlane(rayPos, rayPos + rayDir * 1000000, iris::Plane(facingPlane, 10000), hitDist, hitPoint);
		hitPoint.normalize();

		// project to original handle's plane
		auto d = iris::Vec3::dotProduct(plane, hitPoint);
		hitPoint = hitPoint - plane*d;
	}

	switch (axis)
	{
	case GizmoAxis::X:
		angle = -qAtan2(-hitPoint.y(), hitPoint.z());
		//qDebug() << angle;
		break;
	case GizmoAxis::Y:
		angle = -qAtan2(hitPoint.x(), hitPoint.z());
		//qDebug() << angle;
		break;
	case GizmoAxis::Z:
		angle = -qAtan2(hitPoint.y(), hitPoint.x());
		//qDebug() << angle;
		break;
	}

	angle = qRadiansToDegrees(angle);

	return true;
}

RotationGizmo::RotationGizmo() :
	Gizmo()
{
	handles[0] = new RotationHandle(this, GizmoAxis::X);
	handles[1] = new RotationHandle(this, GizmoAxis::Y);
	handles[2] = new RotationHandle(this, GizmoAxis::Z);

	loadAssets();
	//handle->setHandleColor(QColor(255, 255, 255));

	dragging = false;
	draggedHandle = nullptr;
}

void RotationGizmo::loadAssets()
{
	// Procedural rings (gizmomeshes.cpp): one thin circle per axis instead of
	// the old fat bands, plus a screen-facing outer ring added in drawItems.
	// Radius 1 like the old OBJs, so the analytic hit-testing is untouched.
	handleMeshes.append(GizmoMeshes::rotationRing(GizmoAxis::X));
	handleMeshes.append(GizmoMeshes::rotationRing(GizmoAxis::Y));
	handleMeshes.append(GizmoMeshes::rotationRing(GizmoAxis::Z));
	screenRingMesh = GizmoMeshes::screenRing();


	// create circle
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
	//circleMesh = iris::Mesh::create((void*)points.constData(), points.size() * sizeof(float), points.size(), layout);
}

bool RotationGizmo::isDragging()
{
	return dragging;
}

void RotationGizmo::startDragging(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	trans = Gizmo::getTransform();
	//qDebug() << "drag starting";
	draggedHandle = getHitHandle(rayPos, rayDir, startAngle);
	if (draggedHandle == nullptr) {
		dragging = false; // end dragging if no handle was actually hit
		return;
	}

	draggedHandle->getHitAngle(rayPos, rayDir, startAngle);

	nodeStartRot = selectedNode->getLocalRot().normalized();
	dragging = true;
	setInitialTransform();
}

void RotationGizmo::endDragging()
{
	dragging = false;
	draggedHandle = nullptr;
	trans = Gizmo::getTransform();

	// undo-redo
	createUndoAction();
}

void RotationGizmo::drag(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	//qDebug() << "dragging";
	if (draggedHandle == nullptr) {
		//dragging = false;
		return;
	}
	//qDebug()<<"sliding";
	float hitAngle;
	draggedHandle->getHitAngle(rayPos, rayDir, hitAngle);

	// move node along line
	// do snapping here as well
	auto diff = startAngle - hitAngle;
	auto mods = QApplication::keyboardModifiers();
	if (mods.testFlag(Qt::ControlModifier)) {
		diff = Gizmo::snap(diff, SnapSettings::rotateSize());
	}

	iris::Quat rot;

	switch (draggedHandle->axis) {
		case GizmoAxis::X:
			rot = iris::Quat::fromEulerAngles(diff, 0, 0);
			break;
		case GizmoAxis::Y:
			rot = iris::Quat::fromEulerAngles(0, diff, 0);
			break;
		case GizmoAxis::Z:
			rot = iris::Quat::fromEulerAngles(0, 0, diff);
			break;
	}

	//qDebug() << rot.toEulerAngles();
	//selectedNode->setLocalRot(nodeStartRot * rot);
	if (transformSpace == GizmoTransformSpace::Global)
		selectedNode->setLocalRot(rot * nodeStartRot);
	else
		selectedNode->setLocalRot(nodeStartRot * rot);

	if (services && services->sceneEdit) services->sceneEdit->notifyTransformChanged();
}

bool RotationGizmo::isHit(iris::Vec3 rayPos, iris::Vec3 rayDir)
{
	trans = Gizmo::getTransform();
	for (auto i = 0; i< 3; i++) {
		if (handles[i]->isHit(rayPos, rayDir))
		{
			//handle->setHandleColor(QColor(255, 255, 255));
			return true;
		}
	}

	return false;
}

// returns hit position of the hit handle
RotationHandle* RotationGizmo::getHitHandle(iris::Vec3 rayPos, iris::Vec3 rayDir, float& hitAngle)
{
	RotationHandle* closestHandle = nullptr;
	float closestDistance = 10000000;

	for (auto i = 0; i<3; i++)
	{
		if (handles[i]->isHit(rayPos, rayDir)) {
			float angle;
			auto hit = handles[i]->getHitAngle(rayPos, rayDir, angle);
			auto dist = hitPos.distanceToPoint(rayPos);
			if (dist < closestDistance) {
				closestHandle = handles[i];
				closestDistance = dist;
				hitAngle = angle;
			}
		}
	}

	return closestHandle;
}

iris::Mat4 RotationGizmo::getTransform()
{
	return trans;
	//return Gizmo::getTransform();
}

void RotationGizmo::setTransformSpace(GizmoTransformSpace transformSpace)
{
	this->transformSpace = transformSpace;
	trans = Gizmo::getTransform();
}

void RotationGizmo::setSelectedNode(iris::SceneNodePtr node)
{
	selectedNode = node;
	trans = Gizmo::getTransform();
}

QVector<GizmoDrawItem> RotationGizmo::drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir)
{
	QVector<GizmoDrawItem> items;
	if (!selectedNode) return items;
	const QColor highlight(255, 255, 0);
	if (dragging) {
		for (int i = 0; i < 3; i++) {
			if (handles[i] != draggedHandle) continue;
			auto transform = Gizmo::getTransform();
			transform.scale(getGizmoScale() * handles[i]->handleScale);
			items.append({ handleMeshes[i], transform, highlight });
		}
		return items;
	}
	float hitAngle = 0.0f;
	auto hitHandle = getHitHandle(rayPos, rayDir, hitAngle);
	for (int i = 0; i < 3; i++) {
		auto transform = Gizmo::getTransform();
		transform.scale(getGizmoScale() * handles[i]->handleScale);
		items.append({ handleMeshes[i], transform, handles[i] == hitHandle ? highlight : handles[i]->getHandleColor() });
	}
	// Screen-facing outer circle framing the three axis rings — visual only
	// (there is no fourth handle behind it), always oriented at the camera.
	if (screenRingMesh) {
		iris::Mat4 t;
		t.translate(Gizmo::getTransform().column(3).toVector3D());
		if (!viewDir.isNull())
			t.rotate(iris::Quat::rotationTo(iris::Vec3(0, 0, 1), -viewDir.normalized()));
		t.scale(getGizmoScale() * handles[0]->handleScale);
		items.append({ screenRingMesh, t, QColor(205, 205, 205) });
	}
	return items;
}
