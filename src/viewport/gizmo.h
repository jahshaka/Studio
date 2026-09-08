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

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QtMath>
#include <QColor>
#include <QVector>
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
    iris::Vec3   handlePosition;
    iris::Vec3   handleScale;
    iris::Quat handleRotation;

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

    void setHandleScale(const iris::Vec3& scale) {
        this->handleScale = scale;
    }

    iris::Vec3 getHandleScale() const {
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
    iris::Mat4 transform;
    QColor colour;
};

struct StudioServices;

/// The calibration constant of Gizmo::updateSize (see the long note there).
/// gizmoScale = k * distance * tan(EFFECTIVE fov/2) keeps the gizmo the same
/// fraction of the frame at every angle of view, every window shape and every
/// distance; k is fixed at the value that reproduces the pre-2026-09-07 look at
/// the default 45-degree camera EXACTLY. The fov must be the camera's
/// effectiveFovDegrees() — the angle RENDERED — and never the authored `angle`
/// (the 2026-09-08 "the gizmo is huge in the Showroom" report).
constexpr float kGizmoScreenFraction = 4.33f;

class Gizmo
{
protected:
	/// Undo pushes and transform-refresh notifications go through the
	/// services (Phase 4: was UiManager's statics). Nullable in tests.
	StudioServices* services = nullptr;

	iris::SceneNodePtr selectedNode;
	GizmoTransformSpace transformSpace;
	float gizmoScale;

	iris::Vec3 oldPos, oldScale;
	iris::Quat oldRot;

public:
	Gizmo();
	void setServices(StudioServices* s) { services = s; }
	virtual void updateSize(iris::CameraNodePtr camera);
	float getGizmoScale();

	virtual void setTransformSpace(GizmoTransformSpace transformSpace);
	/// The space this gizmo drags in. Read by the viewport for
	/// editor.gizmoSpace and by the toolbar, which used to guess.
	GizmoTransformSpace getTransformSpace() const { return transformSpace; }
	virtual void setSelectedNode(iris::SceneNodePtr node);
	void clearSelectedNode();

	// undo-redo
	void setInitialTransform();
	void createUndoAction();

	static iris::Vec3 snap(iris::Vec3 pos, float gridSize);
	static float snap(float value, float gridSize);

	// returns transform of the gizmo, not the scene node
	// the transform is calculated based on the transform's space (local or global)
	virtual iris::Mat4 getTransform();
	virtual bool isHit(iris::Vec3 rayPos, iris::Vec3 rayDir);

	virtual bool isDragging() = 0;
	virtual void startDragging(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) = 0;
	virtual void endDragging() = 0;
	virtual void drag(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) = 0;

	/// Renderer-independent description of render(). Empty when nothing is selected.
	virtual QVector<GizmoDrawItem> drawItems(iris::Vec3 rayPos, iris::Vec3 rayDir, iris::Vec3 viewDir) = 0;
};

#endif // GIZMOHANDLE_H
