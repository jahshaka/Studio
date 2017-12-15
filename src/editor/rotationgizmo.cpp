#include "rotationgizmo.h"
#include <QOpenGLFunctions_3_2_Core>
#include <qopenglshaderprogram.h>

#include "irisgl/src/math/intersectionhelper.h"
#include "irisgl/src/math/mathhelper.h"
#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/graphics/graphicshelper.h"
#include "uimanager.h"
#include "../commands/transfrormscenenodecommand.h"
#include "irisgl/src/math/mathhelper.h"

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
	auto worldToGizmo = gizmoTransform.inverted();

	rayPos = worldToGizmo * rayPos;
	rayDir = QQuaternion::fromRotationMatrix(worldToGizmo.normalMatrix()).normalized() * rayDir;

	QVector3D hitPoint;
	float hitDist;
	if (iris::IntersectionHelper::intersectSegmentPlane(rayPos, rayPos + rayDir * 1000000, iris::Plane(plane, 0), hitDist, hitPoint)) {
		float innerRadius = (handleRadius - (handleRadiusSize / 2.0f)) *handleScale * gizmo->getGizmoScale();
		float outerRadius = (handleRadius + (handleRadiusSize / 2.0f)) *handleScale * gizmo->getGizmoScale();
		float distToCenter = hitPoint.length();
		//qDebug() << distToCenter;

		if (distToCenter > innerRadius && distToCenter < outerRadius) {
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
}

bool RotationGizmo::isDragging()
{
	return dragging;
}

void RotationGizmo::startDragging(QVector3D rayPos, QVector3D rayDir)
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
}

void RotationGizmo::endDragging()
{
	dragging = false;
	draggedHandle = nullptr;
}

void RotationGizmo::drag(QVector3D rayPos, QVector3D rayDir)
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

void RotationGizmo::render(QOpenGLFunctions_3_2_Core* gl, QMatrix4x4& viewMatrix, QMatrix4x4& projMatrix)
{
	gl->glClear(GL_DEPTH_BUFFER_BIT);
	//gl->glDisable(GL_DEPTH_TEST);
	shader->bind();

	shader->setUniformValue("u_viewMatrix", viewMatrix);
	shader->setUniformValue("u_projMatrix", projMatrix);
	shader->setUniformValue("showHalf", true);

	if (dragging) {
		for (int i = 0; i < 3; i++) {
			if (handles[i] == draggedHandle) {
				//auto transform = this->getTransform();
				auto transform = Gizmo::getTransform();
				transform.scale(getGizmoScale() * handles[i]->handleScale);
				shader->setUniformValue("color", handles[i]->getHandleColor());
				shader->setUniformValue("u_worldMatrix", transform);
				//handles[i]->draw(gl, shader);
				handleMeshes[i]->draw(gl, shader);
			}
		}
	}
	else
	{
		for (int i = 0; i < 3; i++) {
			auto transform = Gizmo::getTransform();
			transform.scale(getGizmoScale() * handles[i]->handleScale);
			shader->setUniformValue("color", handles[i]->getHandleColor());
			shader->setUniformValue("u_worldMatrix", transform);
			//handles[i]->draw(gl, shader);
			handleMeshes[i]->draw(gl, shader);
		}
	}

	shader->release();
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