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
	float handleScale = 0.05f;
	float handleRadius = 0.05f;
	float handleLength = 1.7f;

	TranslationHandle(Gizmo* gizmo, GizmoAxis axis);

	// check if an actual hit is made
    bool isHit(QVector3D rayPos, QVector3D rayDir);

	// assumes hit was already confirmed
    QVector3D getHitPos(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir);
};

class TranslationGizmo : public Gizmo
{
    iris::MeshPtr handleMesh;
	iris::MeshPtr centerMesh;
	iris::MeshPtr circleMesh;
    QVector<iris::MeshPtr> handleMeshes;

    QOpenGLShaderProgram* shader;
	iris::ShaderPtr lineShader;

    QVector<TranslationHandle*> handles;

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
	void startDragging(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir);
	void endDragging();
	void drag(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir);

	bool isHit(QVector3D rayPos, QVector3D rayDir);

	// hitPos is the hit position of the hit handle
	TranslationHandle* getHitHandle(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir, QVector3D& hitPos);
	void render(iris::GraphicsDevicePtr device, QVector3D rayPos, QVector3D rayDir, QVector3D viewDir, QMatrix4x4& viewMatrix, QMatrix4x4& projMatrix);
};


#endif // TRANSLATIONGIZMO_H
