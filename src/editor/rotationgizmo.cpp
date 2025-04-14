/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "rotationgizmo.h"
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShaderProgram>
#include <QApplication>

#include "irisgl/src/math/intersectionhelper.h"
#include "irisgl/src/math/mathhelper.h"
#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/graphics/graphicshelper.h"
#include "irisgl/src/graphics/graphicsdevice.h"
#include "irisgl/src/graphics/shader.h"
#include "uimanager.h"
#include "../commands/transfrormscenenodecommand.h"
#include "irisgl/src/math/mathhelper.h"
#include "uimanager.h"
#include "../widgets/scenenodepropertieswidget.h"

#define DEFAULT_SNAP_LENGTH 10

RotationHandle::RotationHandle(Gizmo* gizmo, GizmoAxis axis)
{
	this->gizmo = gizmo;
	this->axis = axis;

	switch (axis) {
	case GizmoAxis::X:
		handleExtent = QVector3D(1, 0, 0);
		plane = QVector3D(1, 0, 0);
		setHandleColor(QColor(255, 0, 0));
		break;
	case GizmoAxis::Y:
		handleExtent = QVector3D(0, 1, 0);
		plane = QVector3D(0, 1, 0);
		setHandleColor(QColor(0, 255, 0));
		break;
	case GizmoAxis::Z:
		handleExtent = QVector3D(0, 0, 1);
		plane = QVector3D(0, 0, 1);
		setHandleColor(QColor(0, 0, 255));
		break;
	}
}

bool RotationHandle::isHit(QVector3D rayPos, QVector3D rayDir)
{
	auto gizmoTransform = gizmo->getTransform();
	gizmoTransform.scale(handleScale * gizmo->getGizmoScale());
	auto worldToGizmo = gizmoTransform.inverted();

	rayPos = worldToGizmo * rayPos;
	//rayDir = QQuaternion::fromRotationMatrix(worldToGizmo.normalMatrix()).normalized() * rayDir;
	rayDir = (worldToGizmo * QVector4D(rayDir, 0)).toVector3D();

	QVector3D hitPoint;
	float hitDist;
	QVector3D hitPlane;
	if (QVector3D::dotProduct(plane, rayDir) < 0)
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
		if (QVector3D::dotProduct(hitPoint.normalized(), rayPos.normalized()) < 0)
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

bool RotationHandle::getHitAngle(QVector3D rayPos, QVector3D rayDir, float& angle)
{
	auto gizmoTransform = gizmo->getTransform();
	auto worldToGizmo = gizmoTransform.inverted();

	rayPos = worldToGizmo * rayPos;
	rayDir = QQuaternion::fromRotationMatrix(worldToGizmo.normalMatrix()).normalized() * rayDir;

	QVector3D hitPoint;
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
		auto d = QVector3D::dotProduct(plane, hitPoint);
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
	handleMeshes.append(iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/models/rot_x.obj")));
	handleMeshes.append(iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/models/rot_y.obj")));
	handleMeshes.append(iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/models/rot_z.obj")));

	shader = iris::GraphicsHelper::loadShader(
		IrisUtils::getAbsoluteAssetPath("app/shaders/gizmo.vert"),
		IrisUtils::getAbsoluteAssetPath("app/shaders/gizmo.frag"));

	lineShader = iris::Shader::load(
		IrisUtils::getAbsoluteAssetPath("app/shaders/gizmo_line.vert"),
		IrisUtils::getAbsoluteAssetPath("app/shaders/color.frag"));

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
	layout.addAttrib(iris::VertexAttribUsage::Position, GL_FLOAT, 3, sizeof(float) * 3);

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

void RotationGizmo::startDragging(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir)
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

void RotationGizmo::drag(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir)
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
		diff = Gizmo::snap(diff, DEFAULT_SNAP_LENGTH);
	}

	QQuaternion rot;

	switch (draggedHandle->axis) {
		case GizmoAxis::X:
			rot = QQuaternion::fromEulerAngles(diff, 0, 0);
			break;
		case GizmoAxis::Y:
			rot = QQuaternion::fromEulerAngles(0, diff, 0);
			break;
		case GizmoAxis::Z:
			rot = QQuaternion::fromEulerAngles(0, 0, diff);
			break;
	}

	//qDebug() << rot.toEulerAngles();
	//selectedNode->setLocalRot(nodeStartRot * rot);
	if (transformSpace == GizmoTransformSpace::Global)
		selectedNode->setLocalRot(rot * nodeStartRot);
	else
		selectedNode->setLocalRot(nodeStartRot * rot);

	UiManager::propertyWidget->refreshTransform();
}

bool RotationGizmo::isHit(QVector3D rayPos, QVector3D rayDir)
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
RotationHandle* RotationGizmo::getHitHandle(QVector3D rayPos, QVector3D rayDir, float& hitAngle)
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

void RotationGizmo::render(iris::GraphicsDevicePtr device, QVector3D rayPos, QVector3D rayDir, QVector3D viewDir, QMatrix4x4& viewMatrix, QMatrix4x4& projMatrix)
{
	auto gl = device->getGL();
	gl->glClear(GL_DEPTH_BUFFER_BIT);
	//gl->glDisable(GL_DEPTH_TEST);
	shader->bind();

	shader->setUniformValue("u_viewMatrix", viewMatrix);
	shader->setUniformValue("u_projMatrix", projMatrix);
	shader->setUniformValue("showHalf", true);
	gl->glEnable(GL_BLEND);
	gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	if (dragging) {
		for (int i = 0; i < 3; i++) {
			if (handles[i] == draggedHandle) {
				//auto transform = this->getTransform();
				auto transform = Gizmo::getTransform();
				transform.scale(getGizmoScale() * handles[i]->handleScale);
				//shader->setUniformValue("color", handles[i]->getHandleColor());
				shader->setUniformValue("color", QColor(255, 255, 0));
				shader->setUniformValue("u_worldMatrix", transform);
				//handles[i]->draw(gl, shader);
				handleMeshes[i]->draw(device);
			}
		}
	}
	else
	{
		float hitAngle;
		auto hitHandle = getHitHandle(rayPos, rayDir, hitAngle);
		for (int i = 0; i < 3; i++) {
			auto transform = Gizmo::getTransform();
			transform.scale(getGizmoScale() * handles[i]->handleScale);
			shader->setUniformValue("u_worldMatrix", transform);
			//handles[i]->draw(gl, shader);

			if (handles[i] == hitHandle)
				shader->setUniformValue("color", QColor(255, 255, 0));
			else
				shader->setUniformValue("color", handles[i]->getHandleColor());
			handleMeshes[i]->draw(device);
		}

		//shader->setUniformValue("showHalf", false);
		device->setShader(lineShader);
		device->setShaderUniform("u_viewMatrix", viewMatrix);
		device->setShaderUniform("u_projMatrix", projMatrix);

		auto transform = Gizmo::getTransform();
		//transform.scale(getGizmoScale());
		device->setShaderUniform("u_worldMatrix", transform);
		device->setShaderUniform("color", QColor(255, 255, 255));
		device->setShaderUniform("scale", getGizmoScale() * 0.1f);
		circleMesh->draw(device);

		device->setShaderUniform("color", QColor(40, 40, 40));
		device->setShaderUniform("scale", getGizmoScale() * 0.085f);
		circleMesh->draw(device);
	}

	shader->release();
	gl->glDisable(GL_BLEND);
}

QMatrix4x4 RotationGizmo::getTransform()
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