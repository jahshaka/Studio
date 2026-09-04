/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCALEGIZMO_H
#define SCALEGIZMO_H

#include "irisgl/core/math/vec.h"
#include "viewport/gizmo.h"

class ScaleHandle : public GizmoHandle
{
public:
	Gizmo* gizmo;

	GizmoAxis axis;
	iris::Vec3 handleExtent;// local extent of the gizmo
	QVector<iris::Vec3> planes;// for hit detection
	float handleScale = 0.05f;
	float handleLength = 1.5;

	ScaleHandle(Gizmo* gizmo, GizmoAxis axis);

	bool isHit(iris::Vec3 rayPos, iris::Vec3 rayDir);
	iris::Vec3 getHitPos(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);
};

class ScaleGizmo : public Gizmo
{
	iris::MeshPtr handleMesh;
	iris::MeshPtr centerMesh;
	QVector<iris::MeshPtr> handleMeshes;


	QVector<ScaleHandle*> handles;

	// initial hit position
	iris::Vec3 hitPos;
	iris::Vec3 nodeStartPos;

	// need to keep track of this to know whether or not
	// to invert the scale on uniform scaling
	// it's (hitPos - nodeStartPos).normalized();
	iris::Vec3 hitDir;
	ScaleHandle* draggedHandle;
	int draggedHandleIndex;
	iris::Vec3 startScale;

	// just so the handle looks scaled when dragged
	iris::Vec3 handleVisualScale;

	bool dragging;
private:
	void createAxisLine(GizmoAxis axis);
public:
	ScaleGizmo();

	void loadAssets();

	bool isDragging();
	void startDragging(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);
	void endDragging();
	void drag(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);

	bool isHit(iris::Vec3 rayPos, iris::Vec3 rayDir);

	// hitPos is the hit position of the hit handle
	ScaleHandle* getHitHandle(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir, iris::Vec3& hitPos);
public:
	QVector<GizmoDrawItem> drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) override;
};

#endif // SCALEGIZMO_H
