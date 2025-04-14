/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef ROTATIONGIZMO_H
#define ROTATIONGIZMO_H

//#include "gizmoinstance.h"
#include "gizmo.h"
#include "irisgl/src/graphics/graphicshelper.h"
#include <QMatrix4x4>

class RotationHandle : public GizmoHandle
{
public:
	Gizmo* gizmo;

	GizmoAxis axis;
	QVector3D handleExtent;// local extent of the gizmo
	QVector3D plane;// for hit detection
	float handleScale = 0.08f;
	//float handleRadius = 3.0f;
	float handleRadius = 1.0f;
	float handleRadiusSize = 0.4f;

	RotationHandle(Gizmo* gizmo, GizmoAxis axis);

	bool isHit(QVector3D rayPos, QVector3D rayDir);
	//QVector3D getHitPos(QVector3D rayPos, QVector3D rayDir);
	bool getHitAngle(QVector3D rayPos, QVector3D rayDir, float& angle);
};

class RotationGizmo : public Gizmo
{
	iris::MeshPtr handleMesh;
	iris::MeshPtr circleMesh;
	QVector<iris::MeshPtr> handleMeshes;

	QOpenGLShaderProgram* shader;
	iris::ShaderPtr lineShader;

	RotationHandle* handles[3];

	// initial hit position
	QVector3D hitPos;
	float startAngle;
	QQuaternion nodeStartRot;
	RotationHandle* draggedHandle;
	int draggedHandleIndex;

	QMatrix4x4 trans;
	bool dragging;
public:
	RotationGizmo();

	void loadAssets();

	virtual bool isDragging();
	virtual void startDragging(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir);
	virtual void endDragging();
	virtual void drag(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir);

	virtual bool isHit(QVector3D rayPos, QVector3D rayDir);

	// hitPos is the hit position of the hit handle
	RotationHandle* getHitHandle(QVector3D rayPos, QVector3D rayDir, float& hitAngle);
	virtual void render(iris::GraphicsDevicePtr device, QVector3D rayPos, QVector3D rayDir, QVector3D viewDir, QMatrix4x4& viewMatrix, QMatrix4x4& projMatrix);

	QMatrix4x4 getTransform() override;
	void setTransformSpace(GizmoTransformSpace transformSpace) override;
	void setSelectedNode(iris::SceneNodePtr node) override;
};

#endif // ROTATIONGIZMO_H
