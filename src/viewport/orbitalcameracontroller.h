/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ORBITALCAMERACONTROLLER_H
#define ORBITALCAMERACONTROLLER_H

#include "irisgl/irisglfwd.h"
#include <QPoint>
#include <QVector3D>
#include <QSharedPointer>
#include <QKeyEvent>
//#include "../irisgl/src/core/scenenode.h"
//#include "../irisgl/src/scenegraph/cameranode.h"
#include "viewport/cameracontrollerbase.h"

//class CameraPtr;
namespace iris
{
    class CameraNode;
}

class IEditorViewport;
class OrbitalCameraController:public CameraControllerBase
{
public:
    float lookSpeed;
    float linearSpeed;

    float yaw;
    float pitch;

	float targetYaw;
	float targetPitch;
	float lerpSpeed;

	bool previewMode;

    float rotationSpeed;

	IEditorViewport* sceneWidget;

    QVector3D pivot;
    float distFromPivot;

	iris::CameraNodePtr camera;

    OrbitalCameraController(IEditorViewport* sceneWidget);

    iris::CameraNodePtr  getCamera();
    void setRotationSpeed(float rotationSpeed);
    void setCamera(iris::CameraNodePtr  cam) override;

    void onMouseMove(int x,int y) override;
    void onMouseWheel(int delta) override;

	void onKeyPressed(Qt::Key key);
	void onKeyReleased(Qt::Key key);
	

	void update(float dt) override;

	void focusOnNode(iris::SceneNodePtr sceneNode);

	/// Snap to a canonical view (Views dropdown / view.* shortcuts): orbits
	/// to the axis around the current pivot, animated by update()'s lerp —
	/// the same math the old raw X/Y/Z keys used before they moved to the
	/// ShortcutRegistry.
	void setAxisView(float yawDeg, float pitchDeg);

    void updateCameraRot();

	bool canLeftMouseDrag();

	/// Alt+LMB orbit: the arcball orbits already, so this only re-points the
	/// pivot at the selection (and re-derives the orbit distance) for the
	/// duration of the drag.
	void setAltOrbit(bool active, const QVector3D &pivot) override;
protected:
	virtual void keyReleaseEvent(QKeyEvent *event);
};

#endif // ORBITALCAMERACONTROLLER_H
