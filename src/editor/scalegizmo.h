/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCALEGIZMO_H
#define SCALEGIZMO_H

#include "gizmo.h"
#include "irisgl/src/graphics/graphicshelper.h"

class ScaleHandle : public GizmoHandle
{
public:
	Gizmo* gizmo;

	GizmoAxis axis;
	QVector3D handleExtent;// local extent of the gizmo
	QVector<QVector3D> planes;// for hit detection
	float handleScale = 0.05f;
	float handleLength = 1.5;

	ScaleHandle(Gizmo* gizmo, GizmoAxis axis);

	bool isHit(QVector3D rayPos, QVector3D rayDir);
	QVector3D getHitPos(QVector3D rayPos, QVector3D rayDir);
};

class ScaleGizmo : public Gizmo
{
	iris::MeshPtr handleMesh;
	QVector<iris::MeshPtr> handleMeshes;

	QOpenGLShaderProgram* shader;

	ScaleHandle* handles[3];

	// initial hit position
	QVector3D hitPos;
	QVector3D nodeStartPos;
	ScaleHandle* draggedHandle;
	int draggedHandleIndex;
	QVector3D startScale;

	// just so the handle looks scaled when dragged
	QVector3D handleVisualScale;

	bool dragging;
private:
	void createAxisLine(GizmoAxis axis);
public:
	ScaleGizmo();

	void loadAssets();

	bool isDragging();
	void startDragging(QVector3D rayPos, QVector3D rayDir);
	void endDragging();
	void drag(QVector3D rayPos, QVector3D rayDir);

	bool isHit(QVector3D rayPos, QVector3D rayDir);

	// hitPos is the hit position of the hit handle
	ScaleHandle* getHitHandle(QVector3D rayPos, QVector3D rayDir, QVector3D& hitPos);
	void render(iris::GraphicsDevicePtr device, QVector3D rayPos, QVector3D rayDir, QMatrix4x4& viewMatrix, QMatrix4x4& projMatrix);
};

#endif // SCALEGIZMO_H
