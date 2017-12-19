#include "gizmo.h"

#include "irisgl/src/irisgl.h"
#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/scenegraph/meshnode.h"


Gizmo::Gizmo()
{
	transformSpace = GizmoTransformSpace::Local;
	gizmoScale = 1.0f;
}

// generic update function
void Gizmo::updateSize(iris::CameraNodePtr camera)
{ 
	if (!!selectedNode) {
		float distToCam = (selectedNode->getGlobalPosition() - camera->getGlobalPosition()).length();
		gizmoScale = distToCam / (qTan(camera->angle / 2.0f));
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

// returns transform of the gizmo, not the scene node
// the transform is calculated based on the transform's space (local or global)
QMatrix4x4 Gizmo::getTransform()
{
	//QMatrix4x4 trans;
	//trans.setToIdentity();
	//return trans;

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
		//auto rotMat = selectedNode->getGlobalTransform().normalMatrix();
		trans.rotate(selectedNode->getGlobalRotation());
		//trans.scale(1);
		return trans;
	}
}

bool Gizmo::isHit(QVector3D rayPos, QVector3D rayDir)
{
	return false;
}