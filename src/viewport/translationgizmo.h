/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef TRANSLATIONGIZMO_H
#define TRANSLATIONGIZMO_H

#include "irisgl/core/math/vec.h"
#include "viewport/gizmo.h"
#include "irisgl/irisglfwd.h"


class Gizmo;

class TranslationHandle : public GizmoHandle
{
    public:
    Gizmo* gizmo;

	GizmoAxis axis;
    iris::Vec3 handleExtent;// local extent of the gizmo
	QVector<iris::Vec3> planes;// for hit detection
	float handleScale = 0.05f;
	float handleRadius = 0.05f;
	float handleLength = 1.7f;

	TranslationHandle(Gizmo* gizmo, GizmoAxis axis);

	// check if an actual hit is made
    bool isHit(iris::Vec3 rayPos, iris::Vec3 rayDir);

	// assumes hit was already confirmed
    iris::Vec3 getHitPos(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);
};

class TranslationGizmo : public Gizmo
{
    iris::MeshPtr handleMesh;
	iris::MeshPtr centerMesh;
	iris::MeshPtr circleMesh;
    QVector<iris::MeshPtr> handleMeshes;


    QVector<TranslationHandle*> handles;

    // initial hit position
    iris::Vec3 hitPos;
    iris::Vec3 nodeStartPos;
    TranslationHandle* draggedHandle;
    int draggedHandleIndex;

    bool dragging;

public:
	TranslationGizmo();

	void loadAssets();

	bool isDragging();
	void startDragging(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);
	void endDragging();
	void drag(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir);

	bool isHit(iris::Vec3 rayPos, iris::Vec3 rayDir);

	// hitPos is the hit position of the hit handle
	TranslationHandle* getHitHandle(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir, iris::Vec3& hitPos);
public:
	QVector<GizmoDrawItem> drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) override;
};


#endif // TRANSLATIONGIZMO_H
