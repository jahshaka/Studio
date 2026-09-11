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
#include <QList>
#include <QPointF>
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

/// WHAT THE VIEWPORT IS SHOWING, in the units a mouse event speaks (smoke
/// S15). A gizmo that picks in PIXELS — which the rotation gizmo now does,
/// because a ring seen edge-on has no 3D annulus left to hit — needs the
/// camera it is drawn through and the size of the widget it is drawn in.
/// `width`/`height` are LOGICAL (device-independent) pixels, i.e. exactly
/// `QWidget::width()`/`height()` and exactly the units `QMouseEvent::position`
/// carries, so a pixel TOLERANCE expressed in them is the same physical size
/// on a HiDPI screen as on a plain one; `devicePixelRatio` is carried for the
/// callers that need the physical count (never for the tolerance).
struct GizmoPickView
{
    iris::CameraNodePtr camera;
    float width = 0.0f;
    float height = 0.0f;
    float devicePixelRatio = 1.0f;
    bool isValid() const { return !camera.isNull() && width >= 2.0f && height >= 2.0f; }
};

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

	/// The view the gizmo is currently drawn (and picked) in. Invalid until a
	/// viewport sets it — a gizmo with no pick view picks nothing, which is
	/// what a document-only stand-in wants.
	GizmoPickView pickViewData;

	// ---- GROUP TRANSFORM (EDITOR_MULTISELECT_SPEC §2.4) -------------------
	//
	// The gizmo still belongs to the PRIMARY: it sits at its pivot, hit-tests
	// against it and sizes itself from it, and the three subclasses go on
	// writing exactly that one node. The group is applied AFTER them, as a
	// DELTA read back off the primary — translate is the primary's snapped
	// move applied to everyone, rotate and scale are the primary's delta about
	// the primary's own pivot. That way snapping, axis constraints and the
	// handle maths stay in one place and there is no second implementation of
	// them to drift.
	struct MemberStart
	{
		iris::SceneNodePtr node;
		iris::Vec3 globalPos;
		iris::Quat globalRot;
		iris::Vec3 localPos, localScale;
		iris::Quat localRot;
	};
	/// The D5-reduced selection (the primary included when it is in it).
	/// Empty = single-node drag, i.e. exactly the pre-multiselect behaviour.
	QList<iris::SceneNodePtr> group;
	QVector<MemberStart> groupStart;
	iris::Vec3 pivotStartPos, pivotStartScale;
	iris::Quat pivotStartRot;

	/// Snapshots every member's start transform (called by setInitialTransform,
	/// which every subclass's startDragging already calls).
	void captureGroupStart();

public:
	/// Applies the primary's delta to the rest of the group. Called at the end
	/// of each subclass's drag() — and PUBLIC because it is the whole of the
	/// group-transform maths, which gizmo.group_transform drives directly
	/// (a synthetic ray that hits a handle is a test of the handle geometry,
	/// not of this).
	void applyGroupDelta();

	Gizmo();
	void setServices(StudioServices* s) { services = s; }
	virtual void updateSize(iris::CameraNodePtr camera);
	float getGizmoScale();

	// ---- PIXEL-SPACE PICKING (smoke S15) ---------------------------------
	//
	// The viewport hands the gizmo the view it is drawn through before every
	// pick ray it casts (EngineSceneViewport::mouseRay), so hit-testing can
	// ask "how far is the cursor from this handle ON SCREEN" instead of
	// intersecting handle geometry that may be edge-on to the camera.
	//
	// setPickView also brings the camera's own matrices in step with that
	// widget size (aspect + updateCameraMatrices, exactly as
	// ScenePicker::screenSegment does before building a ray), so projecting
	// through them and unprojecting a ray through them cannot disagree.
	void setPickView(const iris::CameraNodePtr &camera, float width, float height,
	                 float devicePixelRatio = 1.0f);
	const GizmoPickView &pickView() const { return pickViewData; }
	/// World point -> viewport pixel, Qt's top-left origin. False when there
	/// is no pick view or the point is behind the eye.
	bool projectToPixel(const iris::Vec3 &world, QPointF &pixel) const;
	/// THE PIXEL A PICK RAY CAME FROM. Every point of a pick ray projects to
	/// the same pixel (they are collinear with the eye), so this projects one
	/// of them; `reference` — typically the gizmo's own centre — only chooses
	/// a numerically comfortable one, in front of the eye.
	bool rayPixel(const iris::Vec3 &rayPos, const iris::Vec3 &rayDir,
	              const iris::Vec3 &reference, QPointF &pixel) const;

	virtual void setTransformSpace(GizmoTransformSpace transformSpace);
	/// The space this gizmo drags in. Read by the viewport for
	/// editor.gizmoSpace and by the toolbar, which used to guess.
	GizmoTransformSpace getTransformSpace() const { return transformSpace; }
	virtual void setSelectedNode(iris::SceneNodePtr node);
	void clearSelectedNode();
	/// The nodes a drag moves TOGETHER (EDITOR_MULTISELECT_SPEC §2.4) — the
	/// D5-reduced selection set. An empty or single-entry list restores the
	/// single-node behaviour exactly.
	void setGroup(const QList<iris::SceneNodePtr> &nodes);
	int groupSize() const { return group.size(); }

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
