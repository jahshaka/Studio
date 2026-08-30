/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef GIZMOHANDLE_H
#define GIZMOHANDLE_H

#include <QtMath>
#include <QVector3D>
#include <QColor>
#include <QVector>
#include <QQuaternion>
#include <QMatrix4x4>
#include "irisgl/irisglfwd.h"


enum class GizmoAxis
{

	Center,
	X,
	Y,
	Z
};

enum class AxisHandle
{
	Center = 0,
    X,
    Y,
    Z
};

enum class GizmoTransformMode
{
    Translate,
    Rotate,
    Scale
};

enum class GizmoTransformSpace
{
    Local,
    Global
};

enum class GizmoTransformAxis
{
    NONE,
	Center,
    X,
    Y,
    Z,
    XY,
    XZ,
    YZ
};

enum class GizmoPivot
{
    CENTER,
    OBJECT_PIVOT
};

class GizmoHandle
{
private:

    QColor      handleColor;
    QString     handleName;
    QVector3D   handlePosition;
    QVector3D   handleScale;
    QQuaternion handleRotation;

public:

    GizmoHandle()
    {

    }

    void setHandleColor(const QColor& color) {
        this->handleColor = color;
    }

    QColor getHandleColor() const {
        return this->handleColor;
    }

    void setHandleScale(const QVector3D& scale) {
        this->handleScale = scale;
    }

    QVector3D getHandleScale() const {
        return this->handleScale;
    }

    void setHandleName(const QString& name) {
        this->handleName = name;
    }

    QString getHandleName() const {
        return this->handleName;
    }
};

/// What a gizmo would draw this frame, as data: a mesh, its world transform and a
/// flat colour. The engine viewport
/// turns these into on-top overlay items (VIEWPORT_MIGRATION_PLAN.md step 8).
struct GizmoDrawItem
{
    iris::MeshPtr mesh;
    QMatrix4x4 transform;
    QColor colour;
};

struct StudioServices;

class Gizmo
{
protected:
	/// Undo pushes and transform-refresh notifications go through the
	/// services (Phase 4: was UiManager's statics). Nullable in tests.
	StudioServices* services = nullptr;

	iris::SceneNodePtr selectedNode;
	GizmoTransformSpace transformSpace;
	float gizmoScale;

	QVector3D oldPos, oldScale;
	QQuaternion oldRot;

public:
	Gizmo();
	void setServices(StudioServices* s) { services = s; }
	virtual void updateSize(iris::CameraNodePtr camera);
	float getGizmoScale();

	virtual void setTransformSpace(GizmoTransformSpace transformSpace);
	virtual void setSelectedNode(iris::SceneNodePtr node);
	void clearSelectedNode();

	// undo-redo
	void setInitialTransform();
	void createUndoAction();

	static QVector3D snap(QVector3D pos, float gridSize);
	static float snap(float value, float gridSize);

	// returns transform of the gizmo, not the scene node
	// the transform is calculated based on the transform's space (local or global)
	virtual QMatrix4x4 getTransform();
	virtual bool isHit(QVector3D rayPos, QVector3D rayDir);

	virtual bool isDragging() = 0;
	virtual void startDragging(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir) = 0;
	virtual void endDragging() = 0;
	virtual void drag(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir) = 0;

	/// Renderer-independent description of render(). Empty when nothing is selected.
	virtual QVector<GizmoDrawItem> drawItems(QVector3D rayPos, QVector3D rayDir, QVector3D viewDir) = 0;
};

#endif // GIZMOHANDLE_H
