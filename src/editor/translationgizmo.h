/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TRANSLATIONGIZMO_H
#define TRANSLATIONGIZMO_H

#include "gizmo.h"
#include "irisgl/src/irisglfwd.h"


class Gizmo;
class QOpenGLFunctions_3_2_Core;
class QOpenGLShaderProgram;

class TranslationHandle : public GizmoHandle
{
    public:
    Gizmo* gizmo;

	GizmoAxis axis;
    QVector3D handleExtent;// local extent of the gizmo
	QVector<QVector3D> planes;// for hit detection
	float handleScale = 0.1f;
	float handleRadius = 0.05f;
	float handleLength = 1.1f;

	TranslationHandle(Gizmo* gizmo, GizmoAxis axis);

    bool isHit(QVector3D rayPos, QVector3D rayDir);
    QVector3D getHitPos(QVector3D rayPos, QVector3D rayDir);
};

class TranslationGizmo : public Gizmo
{
    iris::MeshPtr handleMesh;
    QVector<iris::MeshPtr> handleMeshes;

    QOpenGLShaderProgram* shader;

    TranslationHandle* handles[3];

    // initial hit position
    QVector3D hitPos;
    QVector3D nodeStartPos;
    TranslationHandle* draggedHandle;
    int draggedHandleIndex;

    bool dragging;

public:
	TranslationGizmo();

	void loadAssets();

	bool isDragging();
	void startDragging(QVector3D rayPos, QVector3D rayDir);
	void endDragging();
	void drag(QVector3D rayPos, QVector3D rayDir);

	bool isHit(QVector3D rayPos, QVector3D rayDir);

	// hitPos is the hit position of the hit handle
	TranslationHandle* getHitHandle(QVector3D rayPos, QVector3D rayDir, QVector3D& hitPos);
	void render(QOpenGLFunctions_3_2_Core* gl, QVector3D rayPos, QVector3D rayDir, QMatrix4x4& viewMatrix, QMatrix4x4& projMatrix);
};


#endif // TRANSLATIONGIZMO_H
