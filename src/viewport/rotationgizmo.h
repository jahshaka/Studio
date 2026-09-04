/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ROTATIONGIZMO_H
#define ROTATIONGIZMO_H

//#include "gizmoinstance.h"
#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "viewport/gizmo.h"

class RotationHandle : public GizmoHandle
{
public:
	Gizmo* gizmo;

	GizmoAxis axis;
	iris::Vec3 handleExtent;// local extent of the gizmo
	iris::Vec3 plane;// for hit detection
	float handleScale = 0.08f;
	//float handleRadius = 3.0f;
	float handleRadius = 1.0f;
	float handleRadiusSize = 0.4f;

	RotationHandle(Gizmo* gizmo, GizmoAxis axis);

	bool isHit(iris::Vec3 rayPos, iris::Vec3 rayDir);
	//iris::Vec3 getHitPos(iris::Vec3 rayPos, iris::Vec3 rayDir);
	bool getHitAngle(iris::Vec3 rayPos, iris::Vec3 rayDir, float& angle);
};

class RotationGizmo : public Gizmo
{
	iris::MeshPtr handleMesh;
	iris::MeshPtr circleMesh;
	iris::MeshPtr screenRingMesh;   // camera-facing outer ring (visual only)
	QVector<iris::MeshPtr> handleMeshes;


	RotationHandle* handles[3];

	// initial hit position
	iris::Vec3 hitPos;
	float startAngle;
	iris::Quat nodeStartRot;
	RotationHandle* draggedHandle;
	int draggedHandleIndex;

	iris::Mat4 trans;
	bool dragging;
public:
	RotationGizmo();

	void loadAssets();

	virtual bool isDragging();
	virtual void startDragging(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);
	virtual void endDragging();
	virtual void drag(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);

	virtual bool isHit(iris::Vec3 rayPos, iris::Vec3 rayDir);

	// hitPos is the hit position of the hit handle
	RotationHandle* getHitHandle(iris::Vec3 rayPos, iris::Vec3 rayDir, float& hitAngle);

	iris::Mat4 getTransform() override;
	void setTransformSpace(GizmoTransformSpace transformSpace) override;
	void setSelectedNode(iris::SceneNodePtr node) override;
public:
	QVector<GizmoDrawItem> drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) override;
};

#endif // ROTATIONGIZMO_H
