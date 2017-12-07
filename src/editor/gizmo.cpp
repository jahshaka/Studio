#include "gizmo.h"

#include "irisgl/src/irisgl.h"
#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/scenegraph/meshnode.h"


Gizmo::Gizmo()
{
	transformSpace = GizmoTransformSpace::Global;
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

// returns transform of the gizmo, not the scene node
// the transform is calculated based on the transform's space (local or global)
QMatrix4x4 Gizmo::getTransform()
{
	//QMatrix4x4 trans;
	//trans.setToIdentity();
	//return trans;

	if (transformSpace == GizmoTransformSpace::Global) {
		QMatrix4x4 trans;
		trans.setToIdentity();
		trans.translate(selectedNode->getGlobalPosition());
		return trans;
	}
	else {
		//todo: remove scale
		return selectedNode->getGlobalTransform();
	}
}

bool Gizmo::isHit(QVector3D rayPos, QVector3D rayDir)
{
	return false;
}